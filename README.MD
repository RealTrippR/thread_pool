## threadpool.c ##
<ins> Brief </ins>

Threadpool.c is a lightweight threadpool implementation written in C11 with a C89 interface and runs on both POSIX and Windows systems.

<ins> **Function List** </ins>

```ThreadPool_New``` - creates a new thread pool <br>
```ThreadPool_Destroy``` - destroys a thread pool <br>
```ThreadPool_LaunchTask``` - launches a task <br>
```ThreadPool_JoinTask``` - joins a task <br>
<ins> **Example**</ins>
```
#include <threadpool.h>
#include <stdlib.h>

#define THREAD_TIMEOUT_MS 30000 // 30 seconds
#define TASK_POOL_THREAD_COUNT 16
#define ITR_COUNT 128
#define BLOCK_SIZE 262144
typedef struct 
{
    uint16_t* rangeBegin;
    uint16_t* rangeLast;
    uint64_t sumOut;
} sumRange_Args;

void* sumRange(void*__args__) {
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
        numbersToSum[i] = i;   
    
    ThreadPoolHandle tpHdl;
    tpHdl.threadCount = TASK_POOL_THREAD_COUNT;
    if (ThreadPool_New(&tpHdl, THREAD_TIMEOUT_MS))
        return EXIT_FAILURE;

    sumRange_Args args[ITR_COUNT];
    for (uint32_t i = 0; i < ITR_COUNT;++i) {
        args[i].rangeBegin = numbersToSum + (i * BLOCK_SIZE);
        args[i].rangeLast = (numbersToSum + i * BLOCK_SIZE + BLOCK_SIZE-1u);
        args[i].sumOut=0u;
    }

    ThreadPoolTaskHandle taskHdls[ITR_COUNT];

    for (uint32_t i = 0; i < ITR_COUNT; ++i)  {
        ThreadPoolTask task;
        task.args = args+i;
        task.func = sumRange;
        if (ThreadPool_LaunchTask(tpHdl, task, taskHdls+i))
            return EXIT_FAILURE;
    }
    for (uint32_t i = 0; i < ITR_COUNT; ++i)
        ThreadPool_JoinTask(taskHdls+i);

    return EXIT_SUCCESS;
}
```
<ins> **Todo** </ins>
- Fix segfault when cthreads_thread_detatch fails

<ins> **Naming Conventions** </ins>
- Preprocessor Macros: UPPER_SNAKE_CASE
- Function Names: CamelCase
- Variable names: pascalCase
- Enum Types: CamelCase
- Enum Values: UPPER_SNAKE_CASE
