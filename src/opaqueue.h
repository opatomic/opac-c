/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPAQUEUE_H_
#define OPAQUEUE_H_


#ifndef OPA_NOTHREADS
#include "opamutex.h"
#endif

typedef struct opaqueueItem_s {
	struct opaqueueItem_s* next;
} opaqueueItem;

typedef struct {
	opaqueueItem* head;
	opaqueueItem* tail;
#ifndef OPA_NOTHREADS
	opamutex m;
	char sync;
#endif
} opaqueue;

void opaqueueInit(opaqueue* q);
#ifndef OPA_NOTHREADS
void opaqueueInitMT(opaqueue* q);
void opaqueueClose(opaqueue* q);
#endif

// returns 0 if queue was not empty; non-zero if queue was empty before modified
int opaqueuePush(opaqueue* q, opaqueueItem* item);
opaqueueItem* opaqueuePoll(opaqueue* q);


#endif
