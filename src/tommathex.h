/*
 * Copyright 2018-2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef TOMMATHEX_H_
#define TOMMATHEX_H_

#ifdef OPA_USEGMP

#include <gmp.h>

#define mp_int __mpz_struct

#define MP_YES   1
#define MP_NO    0

#define MP_OKAY  0
#define MP_MEM  -2
#define MP_VAL  -3

#define MP_DIGIT_BIT ((const unsigned int)(mp_bits_per_limb))

#define mp_digit mp_limb_t

#define mp_clear mpz_clear
#define mp_count_bits(a) mpz_sizeinbase(a, 2)
#define mp_iszero(a) (mpz_sgn(a) == 0 ? MP_YES : MP_NO)
#define mp_isneg(a) (mpz_sgn(a) < 0 ? MP_YES : MP_NO)

#define mp_zero(a) mp_set(a, 0)
#define mp_set mp_set_long_long

int mp_init(mp_int* a);
int mp_init_copy(mp_int* a, const mp_int* b);
int mp_copy(const mp_int* a, mp_int* b);

unsigned long long mp_get_long_long(const mp_int* a);
int mp_set_long_long(mp_int* a, unsigned long long b);

int mp_abs(const mp_int* a, mp_int* b);

int mp_add(const mp_int* a, const mp_int* b, mp_int* c);
int mp_sub(const mp_int* a, const mp_int* b, mp_int* c);
int mp_mul(const mp_int* a, const mp_int* b, mp_int* c);

int mp_add_d(const mp_int* a, mp_digit b, mp_int* c);
int mp_mul_d(const mp_int* a, mp_digit b, mp_int* c);
int mp_div_d(const mp_int* a, mp_digit b, mp_int* c, mp_digit* d);

int mp_import(mp_int* rop, size_t count, int order, size_t size, int endian, size_t nails, const void* op);

int mp_toradix_n(const mp_int* a, char* str, int radix, int maxlen);

#else
#include "tommath.h"
#endif

int mp_to_decimal_n(const mp_int *a, char *str, size_t maxlen);

#endif
