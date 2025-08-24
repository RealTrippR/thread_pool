#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#define THREAD_POOL_API

#include <stdint.h>
#include <errno.h>
#include <stdatomic.h>

typedef  struct 
{
    uint16_t id;
    uint16_t threadCount;
} ThreadPoolHandle;

typedef struct
{
    _Atomic uint8_t state;
} ThreadPoolTaskHandle;

typedef struct
{
    void* args;
    void* (*func)(void*);
    ThreadPoolTaskHandle* hdl;
} ThreadPoolTask;


THREAD_POOL_API errno_t ThreadPoolNew(ThreadPoolHandle*);

THREAD_POOL_API errno_t ThreadPoolDestroy(ThreadPoolHandle*);

THREAD_POOL_API errno_t launchTask(ThreadPoolHandle tpHdl, ThreadPoolTask task, ThreadPoolTaskHandle* taskHdlOut);

THREAD_POOL_API errno_t joinTask(ThreadPoolTaskHandle* taskHdl);


#endif