/*
Copyright (C) 2025 Tripp Robins

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "threadpool.h"
#include "cthreads.h"
#include "stdio.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>

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
    u32 id;
    _Atomic u8 stopsRecieved;
    struct cthreads_mutex mutex;
} ThreadPool;

static errno_t activateWorker(ThreadPool* pool, u16 poolId, u16 workerIdx);

static errno_t deactivateWorker(ThreadPool* pool, u16 workerIdx);

static errno_t stopPool(ThreadPool* pool);

static errno_t pauseAllPools();

static errno_t unpauseAllPools();

static errno_t unpauseAllPoolsInRange(u32 startPoolIdx, u32 count);

typedef struct {
    _Atomic u8 flag; //0:unpause, 1:pause, 2:no_command
    _Atomic u32 pausedOrResumedPoolCount;
} PauseState;

PauseState pauseState={2,0};

struct cthreads_mutex globalMutex;
ThreadPool* threadPools=NULL;
u32 threadPoolCount=0u;
u32* threadPoolIdToIndexMap=NULL;
u32 maxId=0u;

typedef struct {
    u16 threadIndex;
    u16 threadPoolId;
    u32 timeoutMS;
} threadWorkerLoopArgs;

static ThreadPool* getThreadPoolFromId(u32 id)
{
    if (id > maxId) {
        return NULL;
    } else {
        const u32 idx = threadPoolIdToIndexMap[id];
        if (idx >= threadPoolCount) {
            return NULL;}
        return threadPools + idx;
    }
}

static ThreadPoolTask popTaskFromTaskQueue(ThreadPool* pool)
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
  
    bool paused=false;
    clock_t lastActiveT=clock();
    while (1)
    {
        u8 pauseFlag = atomic_load(&pauseState.flag);

        if (paused) {
            cthreads_mutex_lock(&globalMutex);
            ThreadPool* pool = getThreadPoolFromId(args.threadPoolId);
            if (!pool) {
                *(u8*)0 = 0;
                cthreads_mutex_unlock(&globalMutex);
                return (void*)-1;
            } else {
                cthreads_mutex_lock(&pool->mutex);
                // check stop mask
                if (pool->stopMask[args.threadIndex]!=0) {
                    u8 stopsRecieved = atomic_load(&pool->stopsRecieved);
                    stopsRecieved++;
                    if (args.threadIndex < pool->activeWorkerCount) {
                        pool->workers[args.threadIndex].active = false;
                        pool->activeWorkerCount--;
                    }
                    atomic_store(&pool->stopsRecieved, stopsRecieved);
                    cthreads_mutex_unlock(&pool->mutex);
                    cthreads_mutex_unlock(&globalMutex);
                    free(__args);
                    return NULL;
                }
                cthreads_mutex_unlock(&pool->mutex);
            }
            cthreads_mutex_unlock(&globalMutex);

            if (pauseFlag==0) {
                paused=false;
                // inc unpause count
                atomic_fetch_add(&pauseState.pausedOrResumedPoolCount, 1u);
                continue;
            }
        } else {
            if (pauseFlag==1) {
                paused=true;
                // inc pause count
                atomic_fetch_add(&pauseState.pausedOrResumedPoolCount, 1u);
                continue;
            }
            ThreadPool* pool = getThreadPoolFromId(args.threadPoolId);
            if (!pool) {
                if (args.threadIndex < pool->activeWorkerCount) {
                    pool->workers[args.threadIndex].active = false;
                    pool->activeWorkerCount--;
                }
                return (void*)-1;
            }

            cthreads_mutex_lock(&pool->mutex);

            // check stop mask
            if (pool->stopMask[args.threadIndex]!=0) {
                u8 stopsRecieved = atomic_load(&pool->stopsRecieved);
                stopsRecieved++;
                if (args.threadIndex < pool->activeWorkerCount) {
                    pool->workers[args.threadIndex].active = false;
                    pool->activeWorkerCount--;
                }
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
    }
};

THREAD_POOL_API errno_t ThreadPool_New(ThreadPoolHandle* th, u32 timeoutMS)
{
    if (threadPoolCount==UINT32_MAX || th->threadCount==0u) {
        return -1;
    }
    th->id = threadPoolCount;

    if (threadPoolCount==0) {
        cthreads_mutex_init(&globalMutex,NULL);
    }

    cthreads_mutex_lock(&globalMutex);

    if (pauseAllPools()) {
        cthreads_mutex_unlock(&globalMutex);
        return -2; }
    
    void* tmpPools = realloc(threadPools,sizeof(ThreadPool)*(threadPoolCount+1));
    void* tmpIdMap = realloc(threadPoolIdToIndexMap, sizeof(threadPoolIdToIndexMap[0]) * ((maxId > th->id ? maxId : th->id)+1));
    struct cthreads_args* argList = malloc(sizeof(struct cthreads_args) * th->threadCount);
    if (!tmpPools||!tmpIdMap||!argList) {
        if (threadPoolCount>0) {
        threadPoolCount--;}
        cthreads_mutex_unlock(&globalMutex);
        return -1;
    }
    if (th->id>maxId) {
        maxId = th->id;
    }
    


    u32 prevPoolCount = threadPoolCount;

    threadPoolCount++;
    threadPools=tmpPools;
    threadPoolIdToIndexMap=tmpIdMap;
    
    
    ThreadPool* pool = threadPools + (threadPoolCount-1);
    pool->workers = calloc(th->threadCount, sizeof(pool->workers[0]));
    pool->workerCount = th->threadCount;
    pool->activeWorkerCount = 0u;
    pool->taskQueue = NULL;
    pool->taskQueueCount = 0u;
    pool->stopMask = calloc(th->threadCount, sizeof(u8));
    pool->stopsRecieved = 0u;
    pool->timeoutMS = timeoutMS;
    pool->id = th->id;
    threadPoolIdToIndexMap[th->id] = threadPoolCount-1;

    cthreads_mutex_unlock(&globalMutex);

    if (cthreads_mutex_init(&pool->mutex, NULL)) {
        unpauseAllPoolsInRange(0,prevPoolCount);
        return -1;
    }


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
        if (cthreads_mutex_init(&worker->mutex, NULL))
            res=-1;
    skip__:
        if (res!=0 || !targs->data){
            /*cleanup*/
            if (pool->workers) {
                free(pool->workers);
            }
            threadPoolCount--;
            tmpPools = realloc(threadPools,sizeof(ThreadPool)*threadPoolCount);
            if (!tmpPools)
                unpauseAllPoolsInRange(0,prevPoolCount);
                return res;
            threadPools=tmpPools;
            unpauseAllPoolsInRange(0,prevPoolCount);
            return res;
        }
    }

    for (i=0; i < th->threadCount; ++i)
    {
        pool->workers[i].active = true;
        pool->activeWorkerCount++;
        int res = cthreads_thread_detach(pool->workers[i].thread);
        if (res!=0) {
            pool->workers[i].active = false;
            pool->activeWorkerCount--;
            unpauseAllPoolsInRange(0,prevPoolCount);
            return res;
        };
    }

    if (unpauseAllPoolsInRange(0,prevPoolCount)) {
        return -1; }
    return 0;
}

THREAD_POOL_API errno_t ThreadPool_Destroy(ThreadPoolHandle* tpHdl)
{
    ThreadPool* th = getThreadPoolFromId(tpHdl->id);
    if (!th)
        return -1;

    errno_t errcode = stopPool(th);

    cthreads_mutex_lock(&globalMutex);

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

    u32 poolIdx = th - threadPools;
    /*update max id and move indices down*/
    if (pauseAllPools()!=0) {
        return -2; }

    cthreads_mutex_destroy(&th->mutex);


    u32 i;
    u32 newMaxId=0;
    for (i=0; i<=maxId;++i)
    {
        ThreadPool* pool = getThreadPoolFromId(i);
        if (pool && pool->id >= maxId) {
            newMaxId = pool->id;
        }
    }

    /*this has to come after the max id is found to ensure correct indexing*/
    for (i=0; i<=maxId;++i)
    {
        if (threadPoolIdToIndexMap[i]>poolIdx) {
            threadPoolIdToIndexMap[i]--;
        }
    }
    
    if (maxId!=newMaxId) {
        maxId = newMaxId;
        void *tmp = realloc(threadPoolIdToIndexMap, sizeof(threadPoolIdToIndexMap[0])*(maxId+1));
        if (!tmp) {
            errno_t errcode = -1;
             // unlock all mutexes
            for (i = 0; i < threadPoolCount; ++i) {
                if (i==poolIdx)
                    continue;
                ThreadPool* pool = threadPools+i;
                if (cthreads_mutex_unlock(&pool->mutex)!=0)
                    errcode = -2;
            }
            if (unpauseAllPools()!=0)
                errcode=-2;

            return -1;
        }
        threadPoolIdToIndexMap=tmp;
    }

    threadPoolCount--;
    if (threadPoolCount==0) {
        cthreads_mutex_destroy(&globalMutex);
        free(threadPools);
        threadPools=NULL;
        free(threadPoolIdToIndexMap);
        threadPoolIdToIndexMap=NULL;
        maxId = 0u;
    } else {
        // move threadpools down
        u64 s = sizeof(threadPools[0])*(threadPoolCount-poolIdx);\
        ThreadPool* src = threadPools+poolIdx+1;
        ThreadPool* dst = threadPools+poolIdx;
        memmove(dst, src, s);

        threadPools = realloc(threadPools,sizeof(threadPools[1]) * threadPoolCount);
    }
    
    if (threadPoolCount>0) {
        cthreads_mutex_unlock(&globalMutex);
    }

    if (unpauseAllPools()!=0) {
        errcode=-2; }
    /* invalidate handle. */
    tpHdl->id = -1;
    return errcode;
}

THREAD_POOL_API errno_t ThreadPool_LaunchTask(ThreadPoolHandle tpHdl, ThreadPoolTask task, ThreadPoolTaskHandle* taskHdl__)
{
    ThreadPoolTaskHandlePRIVATE* taskHdl = (ThreadPoolTaskHandlePRIVATE*)taskHdl__;
    ThreadPool* pool = getThreadPoolFromId(tpHdl.id);
    if (!pool)
        return -1;
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
    atomic_store(&pool->stopsRecieved, 0);
    u32 activeWorkerCount;
    cthreads_mutex_lock(&pool->mutex);
    memset(pool->stopMask, 1, sizeof(u8) * pool->workerCount);
    activeWorkerCount=pool->activeWorkerCount;
    cthreads_mutex_unlock(&pool->mutex);

    u8 stops = atomic_load(&pool->stopsRecieved);
    while (stops!=activeWorkerCount && pool->activeWorkerCount>0)
    {
        stops = atomic_load(&pool->stopsRecieved);
    }
    atomic_store(&pool->stopsRecieved, 0);
    return 0;
}

static errno_t pauseAllPools()
{
    u32 totalWorkerCount = 0;
    u32 i;
    for (i = 0; i < threadPoolCount; ++i)
    {
        cthreads_mutex_lock(&threadPools[i].mutex);
        totalWorkerCount+=threadPools[i].activeWorkerCount;
        cthreads_mutex_unlock(&threadPools[i].mutex);
    }

    u8 flag = atomic_load(&pauseState.flag);
    if (flag==1) {
        return -1; /* flag is already set to paused */
    }

    if (totalWorkerCount==0) {
        atomic_store(&pauseState.flag, 2);
        return 0; }

    atomic_store(&pauseState.pausedOrResumedPoolCount, 0);
    atomic_store(&pauseState.flag, 1);

    u32 c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    while (c!=totalWorkerCount)
    {
        totalWorkerCount=0;
        // must be recomputed every cycle to get an accurate count (Some threads be deactivate before getting the pause flag)
        u32 i;
        for (i = 0; i < threadPoolCount; ++i)
        {
            cthreads_mutex_lock(&threadPools[i].mutex);
            totalWorkerCount+=threadPools[i].activeWorkerCount;
            cthreads_mutex_unlock(&threadPools[i].mutex);
        }
        c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    }
    atomic_store(&pauseState.flag, 2);

    return 0;
}

static errno_t unpauseAllPools()
{
    u32 totalWorkerCount = 0;
    u32 i;
    for (i = 0; i < threadPoolCount; ++i)
    {
        cthreads_mutex_lock(&threadPools[i].mutex);
        totalWorkerCount+=threadPools[i].activeWorkerCount;
        cthreads_mutex_unlock(&threadPools[i].mutex);
    }

    u8 flag = atomic_load(&pauseState.flag);
    if (flag==0) {
        return -1; /* flag is already set to unpaused */
    }

    atomic_store(&pauseState.pausedOrResumedPoolCount, 0);
    atomic_store(&pauseState.flag, 0);

    if (totalWorkerCount==0u) {
        atomic_store(&pauseState.flag, 2);
        return 0; } 

    u32 c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    while (c<totalWorkerCount)
    {
        totalWorkerCount=0;
        // must be recomputed every cycle to get an accurate count (Some threads be deactivate before getting the pause flag)
        u32 i;
        for (i = 0; i < threadPoolCount; ++i)
        {
            cthreads_mutex_lock(&threadPools[i].mutex);
            totalWorkerCount+=threadPools[i].activeWorkerCount;
            cthreads_mutex_unlock(&threadPools[i].mutex);
        }
        c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    }
    atomic_store(&pauseState.flag, 2);
    return 0;
}


static errno_t unpauseAllPoolsInRange(u32 startPoolIdx, u32 count)
{
    u32 totalWorkerCount = 0;
    u32 i;
    for (i = startPoolIdx; i < startPoolIdx+count; ++i)
    {
        cthreads_mutex_lock(&threadPools[i].mutex);
        totalWorkerCount+=threadPools[i].activeWorkerCount;
        cthreads_mutex_unlock(&threadPools[i].mutex);
    }

    u8 flag = atomic_load(&pauseState.flag);
    if (flag==0) {
        atomic_store(&pauseState.flag, 2);
        return -1; /* flag is already set to unpaused */
    }

    atomic_store(&pauseState.pausedOrResumedPoolCount, 0);
    atomic_store(&pauseState.flag, 0);

    if (totalWorkerCount==0u) {
        return 0; } 

    u32 c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    while (c<totalWorkerCount)
    {
        totalWorkerCount=0u;
        // must be recomputed every cycle to get an accurate count (Some threads be deactivate before getting the pause flag)
        u32 i;
        for (i = startPoolIdx; i < startPoolIdx+count; ++i)
        {
            cthreads_mutex_lock(&threadPools[i].mutex);
            totalWorkerCount+=threadPools[i].activeWorkerCount;
            cthreads_mutex_unlock(&threadPools[i].mutex);
        }
        c = atomic_load(&pauseState.pausedOrResumedPoolCount);
    }
    atomic_store(&pauseState.flag, 2);
    return 0;
}

THREAD_POOL_API void ThreadPool_JoinTask(ThreadPoolTaskHandle* taskHdl__)
{
    ThreadPoolTaskHandlePRIVATE* taskHdl = (ThreadPoolTaskHandlePRIVATE*)taskHdl__;
    uint8_t state = atomic_load(&taskHdl->state);
    while (state==0)
    {
        state = atomic_load(&taskHdl->state);
    }
}