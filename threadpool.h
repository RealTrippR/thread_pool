#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#define THREAD_POOL_API

#include <stdint.h>
#include <errno.h>

typedef struct 
{
    uint16_t id;
    uint16_t threadCount;
} ThreadPoolHandle;
typedef struct 
{
    uint8_t __;
} ThreadPoolTaskHandle;

typedef struct
{
    void* args;
    void* (*func)(void*);
    ThreadPoolTaskHandle* hdl;
} ThreadPoolTask;


/*
Creates a new thread pool.
@param ThreadPoolHandle* - a handle to the thread pool.
@param uint32_t timeoutMS* - the maximum time of inactivity in MS before an worker thread will close. 
@return errno_t - 0 upon success, non-zero value upon failure.*/
THREAD_POOL_API errno_t ThreadPoolNew(ThreadPoolHandle*, uint32_t timeoutMS);

/*
Destroys a thread pool.
@param ThreadPoolHandle* - a handle to the thread pool.
@return errno_t - 0 upon success, non-zero value upon failure.*/
THREAD_POOL_API errno_t ThreadPoolDestroy(ThreadPoolHandle*);

/*
Launches a task.
@param ThreadPoolHandle* - a handle to the thread pool.
@param ThreadPoolTask task - the task to launch.
@param ThreadPoolTaskHandle* taskHdlOut - a handle to the task.
@return errno_t - 0 upon success, non-zero value upon failure.*/
THREAD_POOL_API errno_t launchTask(ThreadPoolHandle tpHdl, ThreadPoolTask task, ThreadPoolTaskHandle* taskHdlOut);

/*
Waits for a task to complete 
@param ThreadPoolTaskHandle* - the handle to wait for.
@return errno_t - 0 upon success, non-zero value upon failure.*/
THREAD_POOL_API void joinTask(ThreadPoolTaskHandle* taskHdl);


#endif