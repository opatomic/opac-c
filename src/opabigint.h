/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPABIGINT_H_
#define OPABIGINT_H_

#if defined(OPABIGINT_USE_MBED)
#include "mbedtls/bignum.h"
#define OPABIGINT_LIB_NAME "mbed"
#define OPABIGINT_DIGIT_BITS (sizeof(mbedtls_mpi_uint) * 8)
typedef mbedtls_mpi_uint opabigintDigit;
typedef mbedtls_mpi opabigint;

#elif defined(OPABIGINT_USE_LTM)
#include "tommath.h"
#define OPABIGINT_LIB_NAME "libtommath"
#define OPABIGINT_DIGIT_BITS MP_DIGIT_BIT
typedef mp_digit opabigintDigit;
typedef mp_int opabigint;

#elif defined(OPABIGINT_USE_LTMS)
#include "opabigint_ltms.h"

#elif defined(OPABIGINT_USE_GMP)
#include <gmp.h>
#define OPABIGINT_LIB_NAME "GMP"
#define OPABIGINT_DIGIT_BITS ((const unsigned int)(mp_bits_per_limb))
typedef mp_limb_t opabigintDigit;
typedef __mpz_struct opabigint;

#else

#error opabigint library not defined

#endif


#include <stdint.h>


void opabigintInit(opabigint* a);
void opabigintFree(opabigint* a);

int opabigintIsZero(const opabigint* a);
int opabigintIsNeg(const opabigint* a);
int opabigintIsEven(const opabigint* a);
uint64_t opabigintGetMagU64(const opabigint* a);
size_t opabigintCountBits(const opabigint* a);
int opabigintCompareMag(const opabigint* a, const opabigint* b);

size_t opabigintUsedLimbs(const opabigint* a);
opabigintDigit opabigintGetLimb(const opabigint* a, size_t n);
int opabigintEnsureSpaceForCopy(opabigint* dst, const opabigint* src);

int opabigintInitCopy(opabigint* dst, const opabigint* src);
int opabigintCopy(opabigint* dst, const opabigint* src);

int opabigintAbs(opabigint* dst, const opabigint* src);
int opabigintNegate(opabigint* dst, const opabigint* src);
int opabigintZero(opabigint* a);
int opabigintSetU64(opabigint* a, uint64_t val);

/**
 * res = a + b
 */
int opabigintAdd(opabigint* res, const opabigint* a, const opabigint* b);

/**
 * res = a - b
 */
int opabigintSub(opabigint* res, const opabigint* a, const opabigint* b);

/**
 * res = a * b
 */
int opabigintMul(opabigint* res, const opabigint* a, const opabigint* b);

/**
 * res = a + b
 */
int opabigintAddDig(opabigint* res, const opabigint* a, opabigintDigit b);

/**
 * res = a * b
 */
int opabigintMulDig(opabigint* res, const opabigint* a, opabigintDigit b);

/**
 * a = (b * q) + r
 */
int opabigintDivDig(opabigint* q, opabigintDigit* r, const opabigint* a, opabigintDigit b);

int opabigintReadBytes(opabigint* a, const unsigned char* buff, size_t buffLen);
size_t opabigintWriteBytes(const opabigint* a, int useBigEndian, unsigned char* buff, size_t buffLen);

int opabigintToRadix(const opabigint* a, char* str, size_t space, size_t* pNumWritten, int radix);

#endif
