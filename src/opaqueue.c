/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include "opacore.h"
#include "opaqueue.h"

static void opaqueueLock(opaqueue* q) {
	#ifndef OPA_NOTHREADS
		if (q->sync) {
			opamutexLock(&q->m);
		}
	#else
		UNUSED(q);
	#endif
}

static void opaqueueUnlock(opaqueue* q) {
	#ifndef OPA_NOTHREADS
		if (q->sync) {
			opamutexLock(&q->m);
		}
	#else
		UNUSED(q);
	#endif
}

void opaqueueInit(opaqueue* q) {
	q->head = q->tail = NULL;
	#ifndef OPA_NOTHREADS
		q->sync = 0;
	#endif
}

#ifndef OPA_NOTHREADS
void opaqueueInitMT(opaqueue* q) {
	opaqueueInit(q);
	opamutexInit(&q->m);
	q->sync = 1;
}

void opaqueueClose(opaqueue* q) {
	if (q->sync) {
		opamutexDestroy(&q->m);
	}
}
#endif

int opaqueuePush(opaqueue* q, opaqueueItem* item) {
	int empty;
	item->next = NULL;
	opaqueueLock(q);
	if (q->head == NULL) {
		q->head = item;
		q->tail = item;
		empty = 1;
	} else {
		q->tail->next = item;
		empty = 0;
	}
	opaqueueUnlock(q);
	return empty;
}

opaqueueItem* opaqueuePoll(opaqueue* q) {
	opaqueueLock(q);
	opaqueueItem* item = q->head;
	if (item != NULL) {
		q->head = item->next;
	}
	opaqueueUnlock(q);
	return item;
}
