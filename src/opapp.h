/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPAPP_H_
#define OPAPP_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t state;
	uint8_t utf8State;
	uint8_t varintNextState;
	uint8_t varintLen;
	unsigned int arrayDepth;
	uint64_t varintVal;
} opapp;

typedef struct {
	unsigned int maxArrayDepth;
	char checkUtf8;
	size_t maxBigIntLen;
	uint64_t maxVarDecExp;
	//size_t maxBinLen;
	//size_t maxStrLen;
} opappOptions;

int opappFindEnd(opapp* rc, const uint8_t* buff, size_t len, const uint8_t** pEnd, const opappOptions* opt);

#endif
