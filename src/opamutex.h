/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPAMUTEX_H_
#define OPAMUTEX_H_

#ifndef OPA_NOTHREADS

#ifdef _WIN32
#include <synchapi.h>
typedef CRITICAL_SECTION opamutex;
#else
#include <pthread.h>
typedef pthread_mutex_t opamutex;
#endif

void opamutexInit(opamutex* m);
void opamutexDestroy(opamutex* m);
void opamutexLock(opamutex* m);
void opamutexUnlock(opamutex* m);

#endif

#endif
