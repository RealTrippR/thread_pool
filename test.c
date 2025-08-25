#include "threadpool.h"
#include <crtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define THREAD_TIMEOUT_MS 60000 // 60 seconds
void* printHello(void*__args__)
{
    printf("Hello\n");
}
int main(int argc, char **argv)
{
    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF);
    ThreadPoolHandle tpHdl;
    tpHdl.threadCount = 2;
    if (ThreadPoolNew(&tpHdl, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;

    
    ThreadPoolTask task;
    task.args = NULL;
    task.func = printHello;
    ThreadPoolTaskHandle taskHdl;
    if (launchTask(tpHdl, task, &taskHdl))
        return EXIT_FAILURE;
    joinTask(&taskHdl);

    if (launchTask(tpHdl, task, &taskHdl))
        return EXIT_FAILURE;
    joinTask(&taskHdl);

    if (ThreadPoolDestroy(&tpHdl))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}