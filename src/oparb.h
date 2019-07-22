/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPARB_H_
#define OPARB_H_

#include <stdint.h>

#include "opabuff.h"

typedef struct {
	int err;              // error code that occurred while building request (ie, out of memory)
	const char* errDesc;  // description of error that occurred while building request (may be NULL even if err is nonzero)
	opabuff buff;         // buff containing raw request
	char started;
	char argsStarted;
	unsigned int depth;
} oparb;

void oparbInit(oparb* rb);
void oparbSetCommand(oparb* rb, const char* cmd);
void oparbAddI64(oparb* rb, int64_t arg);
void oparbAddU64(oparb* rb, uint64_t arg);
void oparbAddSO(oparb* rb, const uint8_t* so);
void oparbAddNumStr(oparb* rb, const char* s);
void oparbAddBin(oparb* rb, size_t len, const void* arg);
void oparbAddStr(oparb* rb, size_t len, const void* arg);
void oparbStartArray(oparb* rb);
void oparbStopArray(oparb* rb);
void oparbFinish(oparb* rb);

/**
 * Try to generate a raw request from a string typed by a user. If an error
 * occurs, then "err" is set to nonzero error code in returned struct.
 * Examples:
 *   PING              -> ["PING"]
 *   ECHO hi           -> ["ECHO",["hi"]]
 *   ECHO [arg1[]arg3] -> ["ECHO",["arg1",[],"arg3"]]
 */
oparb oparbParseUserCommand(const char* s);


#endif
