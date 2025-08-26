#include <threadpool.h>
#include <crtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define THREAD_TIMEOUT_MS 5000 // 5 seconds
void* printHello(void*__args__)
{
    printf("Hello\n");
}
int main(int argc, char **argv)
{
    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF);
    ThreadPoolHandle tpHdl;
    ThreadPoolHandle tpHdl2;
    tpHdl.threadCount = 2;
    tpHdl2.threadCount = 4;
    if (ThreadPool_New(&tpHdl, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;
    if (ThreadPool_New(&tpHdl2, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;

    
    ThreadPoolTask task;
    task.args = NULL;
    task.func = printHello;
        
    ThreadPoolTaskHandle taskHdl;
    if (ThreadPool_LaunchTask(tpHdl, task, &taskHdl))
        return EXIT_FAILURE;
    ThreadPool_JoinTask(&taskHdl);
    if (ThreadPool_Destroy(&tpHdl))
       return EXIT_FAILURE;

       if (ThreadPool_LaunchTask(tpHdl2, task, &taskHdl))
        return EXIT_FAILURE;
    ThreadPool_JoinTask(&taskHdl);

    _sleep(50000);

    if (ThreadPool_Destroy(&tpHdl2))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}