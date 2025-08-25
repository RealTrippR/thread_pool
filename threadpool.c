#include "threadpool.h"
#include "cthreads.h"
#include "stdio.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

typedef	uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
typedef float f32;
typedef double f64;


typedef struct
{
    _Atomic uint8_t state;
} ThreadPoolTaskHandlePRIVATE;

typedef struct 
{
    struct cthreads_thread thread;
    struct cthreads_mutex mutex;
    bool active;
    uint16_t incompleteTaskCount;
} WorkerHandle;

typedef struct
{
    ThreadPoolTask* taskQueue;
    WorkerHandle* workers;
    u8* stopMask;
    u16 workerCount;
    u16 activeWorkerCount;
    u16 taskQueueCount;
    u32 timeoutMS;
    _Atomic u8 stopsRecieved;
    struct cthreads_mutex mutex;
} ThreadPool;

static errno_t activateWorker(ThreadPool* pool, u16 poolId, u16 workerIdx);

static errno_t deactivateWorker(ThreadPool* pool, u16 workerIdx);

static errno_t stopPool(ThreadPool* pool);



 ThreadPool* threadPools=NULL;
 u32 threadPoolCount=0u;

 
typedef struct {
    u16 threadIndex;
    u16 threadPoolId;
    u32 timeoutMS;
} threadWorkerLoopArgs;

ThreadPoolTask popTaskFromTaskQueue(ThreadPool* pool)
{
    ThreadPoolTask task = pool->taskQueue[0];
    u16 i;
    for (i = 1; i < pool->taskQueueCount;++i) {
        pool->taskQueue[i-1] = pool->taskQueue[i];
    }
    pool->taskQueueCount--;
    return task;
}

void* threadWorkerLoop(void* __args)
{
    threadWorkerLoopArgs args = *((threadWorkerLoopArgs*)__args);
    ThreadPool* pool = &threadPools[args.threadPoolId];

    clock_t lastActiveT=clock();
    while (1)
    {
        cthreads_mutex_lock(&pool->mutex);
        if (pool->stopMask[args.threadIndex]!=0) {
            u8 stopsRecieved = atomic_load(&pool->stopsRecieved);
            stopsRecieved++;
            pool->workers[args.threadIndex].active = false;
            pool->activeWorkerCount--;
            atomic_store(&pool->stopsRecieved, stopsRecieved);
            cthreads_mutex_unlock(&pool->mutex);
            free(__args);
            return NULL;
        }
        if (pool->taskQueueCount>0) {
            ThreadPoolTask task = popTaskFromTaskQueue(pool);
            ThreadPoolTaskHandlePRIVATE* hdl = (ThreadPoolTaskHandlePRIVATE*)task.hdl;
            task.func(task.args);
            atomic_store(&hdl->state,1);
            lastActiveT = clock();
        }
        if (args.timeoutMS!=0) {
            f64 timeInactiveMS = (f64)(clock() - lastActiveT) / CLOCKS_PER_SEC *  1000.0;
            if (timeInactiveMS>args.timeoutMS) {
                pool->workers[args.threadIndex].active = false;
                pool->activeWorkerCount--;
                free(__args);
                cthreads_mutex_unlock(&pool->mutex);
                return NULL;
            }
        }
        cthreads_mutex_unlock(&pool->mutex);
    }
};

THREAD_POOL_API errno_t ThreadPoolNew(ThreadPoolHandle* th, u32 timeoutMS)
{
    if (threadPoolCount==UINT32_MAX || th->threadCount==0u) {
        return -1;
    }
    threadPoolCount++;
    threadPools = realloc(threadPools,sizeof(ThreadPool)*threadPoolCount);
    struct cthreads_args* argList = malloc(sizeof(struct cthreads_args) * th->threadCount);
    if (!threadPools||!argList) {
        return -1;
    }
    th->id = threadPoolCount-1;
    

    ThreadPool* pool = threadPools + (threadPoolCount-1);
    pool->workers = calloc(th->threadCount, sizeof(pool->workers[0]));
    pool->workerCount = th->threadCount;
    pool->activeWorkerCount = 0u;
    pool->taskQueue = NULL;
    pool->taskQueueCount = 0u;
    pool->stopMask = calloc(th->threadCount, sizeof(u8));
    pool->stopsRecieved = 0u;
    pool->timeoutMS = timeoutMS;

    u32 i;
    for (i = 0; i < th->threadCount; ++i)
    {
    
  
        struct cthreads_args* targs = argList+i;
        targs->func = threadWorkerLoop;
        targs->data = malloc(sizeof(threadWorkerLoopArgs));
    
        if (!targs->data) {
            goto skip__;
        }

        ((threadWorkerLoopArgs*)(targs->data))->threadIndex = (u16)i;
        ((threadWorkerLoopArgs*)(targs->data))->threadPoolId = th->id;
        ((threadWorkerLoopArgs*)(targs->data))->timeoutMS = pool->timeoutMS;

        int res = 0;
        WorkerHandle* worker = pool->workers + i;
        res = cthreads_thread_create(&worker->thread, NULL, targs->func, targs->data, targs);
        cthreads_mutex_init(&worker->mutex, NULL);
    skip__:
        if (res!=0 || !targs->data){
            /*cleanup*/
            if (pool->workers) {
                free(pool->workers);
            }
            threadPoolCount--;
            threadPools = realloc(threadPools,sizeof(ThreadPool)*threadPoolCount);
            return res;
        }
    }

    for (i=0; i < th->threadCount; ++i)
    {
        int res = cthreads_thread_detach(pool->workers[i].thread);
        if (res!=0) {
            return res;
        } else {
            pool->workers[i].active = true;
            pool->activeWorkerCount++;
        }
    }
    return 0;
}

THREAD_POOL_API errno_t ThreadPoolDestroy(ThreadPoolHandle* tpHdl)
{
    if (tpHdl->id>=threadPoolCount)
        return -1;
    
    ThreadPool* th = threadPools + tpHdl->id;

    errno_t errcode = stopPool(th);

    if (th->workers) 
    {
        u32 i;
        for (i = 0; i < th->workerCount; ++i)
        {
            WorkerHandle* worker = th->workers+i;
            cthreads_mutex_destroy(&worker->mutex);
        }

        free(th->workers);
        th->workers=NULL;
    }
    th->workerCount = 0;
    th->activeWorkerCount = 0;
    return errcode;
}

THREAD_POOL_API errno_t launchTask(ThreadPoolHandle tpHdl, ThreadPoolTask task, ThreadPoolTaskHandle* taskHdl__)
{
    if (tpHdl.id >= threadPoolCount)
        return -1;
    
    ThreadPoolTaskHandlePRIVATE* taskHdl = (ThreadPoolTaskHandlePRIVATE*)taskHdl__;
    ThreadPool* pool = threadPools + tpHdl.id;
    cthreads_mutex_lock(&pool->mutex);

    task.hdl = taskHdl__;

    atomic_store(&taskHdl->state,0);
    pool->taskQueueCount++;
    u32 c = ((pool->taskQueueCount+15) / 16) * 16; // ceil to 16
    pool->taskQueue = realloc(pool->taskQueue, sizeof(pool->taskQueue[0]) * c);
    if (pool->taskQueue==NULL) {
        pool->taskQueueCount==0;
        cthreads_mutex_unlock(&pool->mutex);
        return -1;
    }
    pool->taskQueue[pool->taskQueueCount-1] = task;

        
    if (pool->activeWorkerCount<pool->taskQueueCount) {
        u16 diff=pool->taskQueueCount-pool->activeWorkerCount;
        u16 i;
        u16 c=0;
        for (i=0; i < pool->workerCount; i++)
        {
            if (pool->workers[i].active==false) {
                activateWorker(pool, tpHdl.id, i);
                c++;
            }
            if (c==diff)
                break;
        }
    }

    cthreads_mutex_unlock(&pool->mutex);
    return 0;
}

static errno_t activateWorker(ThreadPool* pool, u16 poolId, u16 workerIdx)
{
    if (workerIdx >= pool->workerCount)
        return -1;

    struct cthreads_args targs;
    targs.func = threadWorkerLoop;
    targs.data = malloc(sizeof(threadWorkerLoopArgs));

    if (!targs.data) {
        return -1;
    }

    ((threadWorkerLoopArgs*)(targs.data))->threadIndex = (u16)workerIdx;
    ((threadWorkerLoopArgs*)(targs.data))->threadPoolId = poolId;
    ((threadWorkerLoopArgs*)(targs.data))->timeoutMS = pool->timeoutMS;

    WorkerHandle* worker = pool->workers + workerIdx;
    int res = cthreads_thread_create(&worker->thread, NULL, targs.func, targs.data, &targs);
    if (res!=0)
        return res;
    
    res = cthreads_thread_detach(worker->thread);
    if (res!=0) {
        return res;
    } else {
        worker->active = true;
        pool->activeWorkerCount++;
    }

    return res;
}

static errno_t deactivateWorker(ThreadPool* pool, u16 workerIdx)
{
    if (workerIdx >= pool->workerCount)
        return -1;
    atomic_store(&pool->stopsRecieved, 0);
    pool->stopMask[workerIdx] = 1;
    u8 stops = atomic_load(&pool->stopsRecieved);
    while (stops!=1)
    {
        stops = atomic_load(&pool->stopsRecieved);
    }

    pool->workers[workerIdx].active = false;
    pool->activeWorkerCount--;
    return 0;
}

static errno_t stopPool(ThreadPool* pool)
{
    memset(pool->stopMask, 1, sizeof(u8) * pool->workerCount);
    
    u8 stops = atomic_load(&pool->stopsRecieved);
    while (stops!=pool->workerCount && pool->activeWorkerCount>0)
    {
        stops = atomic_load(&pool->stopsRecieved);
    }
    atomic_store(&pool->stopsRecieved, 0);
    return 0;
}

THREAD_POOL_API void joinTask(ThreadPoolTaskHandle* taskHdl__)
{
    ThreadPoolTaskHandlePRIVATE* taskHdl = (ThreadPoolTaskHandlePRIVATE*)taskHdl__;
    uint8_t state = atomic_load(&taskHdl->state);
    while (state==0)
    {
        state = atomic_load(&taskHdl->state);
    }
}