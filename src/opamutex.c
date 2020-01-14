/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include "opacore.h"
#include "opamutex.h"

#ifndef OPA_NOTHREADS

#ifdef _WIN32

void opamutexInit(opamutex* m) {
	InitializeCriticalSection(m);
}

void opamutexDestroy(opamutex* m) {
	DeleteCriticalSection(m);
}

void opamutexLock(opamutex* m) {
	EnterCriticalSection(m);
}

int opamutexTryLock(opamutex* m) {
	return TryEnterCriticalSection(m);
}

void opamutexUnlock(opamutex* m) {
	LeaveCriticalSection(m);
}


#else

static void opamutexPanicIfErr(int err) {
	if (err) {
		LOGSYSERR(err);
		OPAPANIC("panic due to mutex error");
	}
}

void opamutexInit(opamutex* m) {
	opamutexPanicIfErr(pthread_mutex_init(m, NULL));
}

void opamutexDestroy(opamutex* m) {
	opamutexPanicIfErr(pthread_mutex_destroy(m));
}

void opamutexLock(opamutex* m) {
	opamutexPanicIfErr(pthread_mutex_lock(m));
}

int opamutexTryLock(opamutex* m) {
	int res = pthread_mutex_trylock(m);
	if (res == 0) {
		return 1;
	} else if (res != EBUSY) {
		opamutexPanicIfErr(res);
	}
	return 0;
}

void opamutexUnlock(opamutex* m) {
	opamutexPanicIfErr(pthread_mutex_unlock(m));
}

#endif

#endif
