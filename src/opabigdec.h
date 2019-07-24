/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPABIGDEC_H_
#define OPABIGDEC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef OPA_USEGMP
#include "gmpcompat.h"
#else
#include "tommath.h"
#endif


typedef struct {
	mp_int significand;
	int32_t exponent;
} opabigdec;


int opabigdecInit(opabigdec* a);
int opabigdecInitCopy(const opabigdec* src, opabigdec* dst);
int opabigdecCopy(const opabigdec* src, opabigdec* dst);
void opabigdecClear(opabigdec* a);

int opabigdecIsNeg(const opabigdec* a);
int opabigdecSet64(opabigdec* a, uint64_t val, int isNeg, int32_t exp);
int opabigdecGet64(const opabigdec* a, uint64_t* pVal);

int opabigdecAdd(const opabigdec* a, const opabigdec* b, opabigdec* result);
int opabigdecSub(const opabigdec* a, const opabigdec* b, opabigdec* result);
int opabigdecMul(const opabigdec* a, const opabigdec* b, opabigdec* result);

int opabigdecLoadSO(opabigdec* bd, const uint8_t* so);
size_t opabigdecStoreSO(const opabigdec* val, uint8_t* buff, size_t buffLen);

int opabigdecFromStr(opabigdec* v, const char* str, int radix);
size_t opabigdecMaxStringLen(const opabigdec* a, int radix);
int opabigdecToString(const opabigdec* a, char* str, int radix, size_t space);


#endif
