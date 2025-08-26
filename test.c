#include <threadpool.h>
#include <crtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>
#include <deps/cthreads.h>

#define THREAD_TIMEOUT_MS 10000 // 10 seconds
#define ITR_COUNT 32
#define BLOCK_SIZE 2048
typedef struct 
{
    uint16_t* rangeBegin;
    uint16_t* rangeLast;
    uint64_t sumOut;
} sumRange_Args;

void* sumRange(void*__args__)
{
    sumRange_Args* args = __args__;
    for (uint16_t* ptr = args->rangeBegin; ptr <= args->rangeLast; ++ptr)
    {
        args->sumOut+=*ptr;
    }
    return NULL;
}

uint16_t numbersToSum[ITR_COUNT*BLOCK_SIZE];

int main(int argc, char **argv)
{
    for (uint32_t i = 0; i<ITR_COUNT*BLOCK_SIZE;++i)
    {
        numbersToSum[i] = i;   
    }

    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF);
    ThreadPoolHandle tpHdl;
    tpHdl.threadCount = 8;
    sumRange_Args args[ITR_COUNT];
    for (uint32_t i = 0; i < ITR_COUNT;++i) {
        args[i].rangeBegin = numbersToSum + (i * BLOCK_SIZE);
        args[i].rangeLast = (numbersToSum + i * BLOCK_SIZE + BLOCK_SIZE-1u);
        args[i].sumOut=0u;
    }

    if (ThreadPool_New(&tpHdl, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;

    clock_t t = clock();
    for (uint32_t i = 0; i < ITR_COUNT; ++i) 
    {
        ThreadPoolTask task;
        task.args = args+i;
        task.func = sumRange;
        
        ThreadPoolTaskHandle taskHdl;
        if (ThreadPool_LaunchTask(tpHdl, task, &taskHdl))
            return EXIT_FAILURE;
        ThreadPool_JoinTask(&taskHdl);
    }
    double durationSec = ((double)(clock() - t)) / CLOCKS_PER_SEC;


    uint64_t sum=0;
    for (uint32_t i = 0; i < ITR_COUNT; ++i) {
        sum+=args[i].sumOut;
    }
    printf("==============================================\nduration-seconds (thread pools): %f\n", durationSec);
    printf("Sum: %lu\n", sum);
    t = clock();

    for (uint32_t i = 0; i < ITR_COUNT;++i) {
        args[i].sumOut=0u;
    }

    // launch ITR_COUNT threads and join
    struct cthreads_thread threads[ITR_COUNT];
    struct cthreads_args thr_args[ITR_COUNT];
    for (uint32_t i = 0; i < ITR_COUNT; ++i) 
    {
        thr_args[i].data=args+i;
        thr_args[i].func=sumRange;
        cthreads_thread_create(&threads[i], NULL,thr_args[i].func, thr_args[i].data, thr_args+i);
    }
    for (uint32_t i = 0; i < ITR_COUNT; ++i) 
    {
        cthreads_thread_join(threads[i],NULL);
    }
    durationSec = ((double)(clock() - t)) / CLOCKS_PER_SEC;
    sum=0;
    for (uint32_t i = 0; i < ITR_COUNT; ++i) {
        sum+=args[i].sumOut;
    }
    printf("==============================================\nduration-seconds (1 thread per iteration): %f\n", durationSec);
    printf("Sum: %lu\n", sum);

    if (ThreadPool_Destroy(&tpHdl))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}