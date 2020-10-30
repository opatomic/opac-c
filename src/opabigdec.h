/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPABIGDEC_H_
#define OPABIGDEC_H_

#include <stddef.h>
#include <stdint.h>

#include "opabigint.h"


typedef struct {
	opabigint significand;
	int32_t exponent;
	char inf;
} opabigdec;


void opabigdecInit(opabigdec* a);
int opabigdecInitCopy(const opabigdec* src, opabigdec* dst);
int opabigdecCopy(opabigdec* dst, const opabigdec* src);
void opabigdecClear(opabigdec* a);

int opabigdecIsNeg(const opabigdec* a);
int opabigdecIsZero(const opabigdec* a);
int opabigdecIsFinite(const opabigdec* a);
int opabigdecSet64(opabigdec* a, uint64_t val, int isNeg, int32_t exp);
int opabigdecGetMag64(const opabigdec* a, uint64_t* pVal);

int opabigdecAdd(opabigdec* result, const opabigdec* a, const opabigdec* b);
int opabigdecSub(opabigdec* result, const opabigdec* a, const opabigdec* b);
int opabigdecMul(opabigdec* result, const opabigdec* a, const opabigdec* b);

int opabigdecLoadSO(opabigdec* bd, const uint8_t* so);
size_t opabigdecStoreSO(const opabigdec* val, uint8_t* buff, size_t buffLen);

int opabigdecFromStr(opabigdec* v, const char* str, const char* end, int radix);
size_t opabigdecMaxStringLen(const opabigdec* a, int radix);
int opabigdecToString(const opabigdec* a, char* str, size_t space, size_t* pWritten, int radix);


#endif
