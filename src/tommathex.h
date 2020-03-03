/*
 * Copyright 2018-2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef TOMMATHEX_H_
#define TOMMATHEX_H_

#ifdef OPA_USEGMP

#include <gmp.h>
#include <stdint.h>

#define mp_int __mpz_struct

#define MP_YES   1
#define MP_NO    0

#define MP_OKAY  0
#define MP_MEM  -2
#define MP_VAL  -3
#define MP_BUF  -5

#define MP_DIGIT_BIT ((const unsigned int)(mp_bits_per_limb))

#define mp_digit mp_limb_t

#define mp_clear mpz_clear
#define mp_count_bits(a) mpz_sizeinbase(a, 2)
#define mp_iszero(a) (mpz_sgn(a) == 0 ? MP_YES : MP_NO)
#define mp_isneg(a) (mpz_sgn(a) < 0 ? MP_YES : MP_NO)

#define mp_zero(a) mp_set_u64(a, 0)

typedef int mp_err;

mp_err mp_init(mp_int* a);
mp_err mp_init_copy(mp_int* a, const mp_int* b);
mp_err mp_copy(const mp_int* a, mp_int* b);

uint64_t mp_get_mag_u64(const mp_int* a);
void mp_set_u64(mp_int* a, uint64_t b);

mp_err mp_abs(const mp_int* a, mp_int* b);

mp_err mp_add(const mp_int* a, const mp_int* b, mp_int* c);
mp_err mp_sub(const mp_int* a, const mp_int* b, mp_int* c);
mp_err mp_mul(const mp_int* a, const mp_int* b, mp_int* c);

mp_err mp_add_d(const mp_int* a, mp_digit b, mp_int* c);
mp_err mp_mul_d(const mp_int* a, mp_digit b, mp_int* c);
mp_err mp_div_d(const mp_int* a, mp_digit b, mp_int* c, mp_digit* d);

mp_err mp_unpack(mp_int* rop, size_t count, int order, size_t size, int endian, size_t nails, const void* op);

mp_err mp_to_radix(const mp_int* a, char* str, size_t maxlen, size_t* written, int radix);

#else
#include "tommath.h"
#endif

mp_err mp_to_radix10(const mp_int *a, char *str, size_t maxlen, size_t *written);

#endif
