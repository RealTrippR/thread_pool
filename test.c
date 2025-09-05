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
#include <threadpool.h>
#include <crtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <deps/cthreads.h>

#define THREAD_TIMEOUT_MS 100 // 30 seconds
#define TASK_POOL_THREAD_COUNT 2
#define ITR_COUNT 128
#define BLOCK_SIZE 262144
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
    tpHdl.threadCount = TASK_POOL_THREAD_COUNT;
    ThreadPoolHandle tpHdl2;
    tpHdl2.threadCount = TASK_POOL_THREAD_COUNT;
    
    sumRange_Args args[ITR_COUNT];
    for (uint32_t i = 0; i < ITR_COUNT;++i) {
        args[i].rangeBegin = numbersToSum + (i * BLOCK_SIZE);
        args[i].rangeLast = (numbersToSum + i * BLOCK_SIZE + BLOCK_SIZE-1u);
        args[i].sumOut=0u;
    }

    clock_t t = clock();
    if (ThreadPool_New(&tpHdl, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;
    if (ThreadPool_New(&tpHdl2, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;

    ThreadPoolTaskHandle taskHdls[ITR_COUNT];
    // -- BEGIN TIMING -- //
    for (uint32_t i = 0; i < ITR_COUNT; ++i) 
    {
        ThreadPoolTask task;
        task.args = args+i;
        task.func = sumRange;
        
        if (ThreadPool_LaunchTask(tpHdl, task, taskHdls+i))
            return EXIT_FAILURE;
    }
    for (uint32_t i = 0; i < ITR_COUNT; ++i)
    {
        ThreadPool_JoinTask(taskHdls+i);
    }
    printf("===== PAUSE =====");
    _sleep(500);
    for (uint32_t i = 0; i < ITR_COUNT; ++i)
    {
        ThreadPool_JoinTask(taskHdls+i);
    }
    // -- END TIMING -- //
    double durationSec = ((double)(clock() - t)) / CLOCKS_PER_SEC;

    uint64_t sum=0;
    for (uint32_t i = 0; i < ITR_COUNT; ++i) {
        sum+=args[i].sumOut;
    }
    printf("==============================================\nduration-seconds (thread pools): %f\n", durationSec);
    printf("Sum: %lu\n", sum);

    for (uint32_t i = 0; i < ITR_COUNT;++i) {
        args[i].sumOut=0u;
    }
    // launch ITR_COUNT threads and join
    struct cthreads_thread threads[ITR_COUNT];
    struct cthreads_args thr_args[ITR_COUNT];

    // -- BEGIN TIMING -- //
    t = clock();
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

    // -- END TIMING -- //
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