/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <limits.h>
#include <string.h>

#ifdef OPA_USEGMP
#include "gmpcompat.h"

#define SASSERT(e) switch(0){case 0: case e:;}


int mp_init(mp_int* a) {
	mpz_init(a);
	return MP_OKAY;
}

int mp_init_copy(mp_int* a, const mp_int* b) {
	mpz_init_set(a, b);
	return MP_OKAY;
}

int mp_copy(const mp_int* a, mp_int* b) {
	mpz_set(b, a);
	return MP_OKAY;
}

unsigned long long mp_get_long_long(const mp_int* a) {
	SASSERT(sizeof(unsigned int) == 4);

	unsigned int lsb = mpz_get_ui(a);
	if (mpz_sizeinbase(a, 2) <= 32) {
		return lsb;
	}

	mpz_t tmp;
	mpz_init(tmp);
	mpz_mod_2exp(tmp, a, 64);
	mpz_div_2exp(tmp, tmp, 32);
	unsigned int msb = mpz_get_ui(tmp);
	mpz_clear(tmp);

	return (((unsigned long long)msb) << 32) | lsb;
}

int mp_set_long_long(mp_int* a, unsigned long long b) {
	SASSERT(sizeof(unsigned int) == 4);
	if (b <= UINT_MAX) {
		mpz_set_ui(a, (unsigned int)(b));
	} else {
		mpz_set_ui(a, (unsigned int)(b >> 32));
		mpz_mul_2exp(a, a, 32);
		mpz_add_ui(a, a, (unsigned int)b);
	}
	return MP_OKAY;
}

int mp_abs(const mp_int* a, mp_int* b) {
	mpz_abs(b, a);
	return MP_OKAY;
}

int mp_add(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_add(c, a, b);
	return MP_OKAY;
}

int mp_sub(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_sub(c, a, b);
	return MP_OKAY;
}

int mp_mul(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_mul(c, a, b);
	return MP_OKAY;
}

int mp_add_d(const mp_int* a, mp_digit b, mp_int* c) {
	mpz_add_ui(c, a, b);
	return MP_OKAY;
}

int mp_mul_d(const mp_int* a, mp_digit b, mp_int* c) {
	mpz_mul_ui(c, a, b);
	return MP_OKAY;
}

int mp_div_d(const mp_int* a, mp_digit b, mp_int* c, mp_digit* d) {
	*d = mpz_tdiv_q_ui(c, a, b);
	return MP_OKAY;
}

int mp_import(mp_int* rop, size_t count, int order, size_t size, int endian, size_t nails, const void* op) {
	mpz_import(rop, count, order, size, endian, nails, op);
	return MP_OKAY;
}


/*
int mp_toradix_n(const mp_int* a, char* str, int radix, int maxlen) {
	if ((maxlen <= 0) || (radix < 2) || (radix > 64)) {
		return MP_VAL;
	}
	if (radix > 62) {
		// TODO: add support for radix of 63 & 64. libtommath supports radix up to 64 (similar to base64 encoding)
		return MP_VAL;
	}

	size_t max = (size_t) maxlen;
	size_t szib = mpz_sizeinbase(a, radix);

	// libtommath char mapping: "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/"
	// GMP:
	//  For base in the range 2..36, digits and lower-case letters are used; for -2..-36, digits
	//  and upper-case letters are used; for 37..62, digits, upper-case letters, and lower-case
	//  letters (in that significance order) are used.
	if (radix > 10 && radix < 37) {
		radix = 0 - radix;
	}

	if (max >= szib + 2) {
		mpz_get_str(str, radix, a);
	} else {
		char* allocStr = mpz_get_str(NULL, radix, a);
		if (allocStr == NULL) {
			return MP_MEM;
		}
		size_t realLen = strlen(allocStr);
		size_t numToCopy = realLen + 1 <= max ? realLen : max - 1;
		memcpy(str, allocStr, numToCopy);
		str[numToCopy] = 0;
		void (*freefunc) (void*, size_t);
		mp_get_memory_functions(NULL, NULL, &freefunc);
		freefunc(allocStr, realLen + 1);
	}
	return MP_OKAY;
}
*/


/**
 * Note: the following 2 functions come from libtommath
 * https://github.com/libtom/libtommath/
 * https://github.com/libtom/libtommath/blob/v1.1.0/bn_reverse.c
 * https://github.com/libtom/libtommath/blob/v1.1.0/bn_mp_toradix_n.c
 */
static void bn_reverse(unsigned char *s, int len) {
	int     ix, iy;
	unsigned char t;

	ix = 0;
	iy = len - 1;
	while (ix < iy) {
		t     = s[ix];
		s[ix] = s[iy];
		s[iy] = t;
		++ix;
		--iy;
	}
}

int mp_toradix_n(const mp_int *a, char *str, int radix, int maxlen) {
	static const char *const mp_s_rmap = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
	int     res, digs;
	mp_int  t;
	mp_digit d;
	char   *_s = str;

	/* check range of the maxlen, radix */
	if ((maxlen < 2) || (radix < 2) || (radix > 64)) {
		return MP_VAL;
	}

	/* quick out if its zero */
	if (mp_iszero(a) == MP_YES) {
		*str++ = '0';
		*str = '\0';
		return MP_OKAY;
	}

	if ((res = mp_init_copy(&t, a)) != MP_OKAY) {
		return res;
	}

	/* if it is negative output a - */
	if (mp_isneg(&t)) {
		/* we have to reverse our digits later... but not the - sign!! */
		++_s;

		/* store the flag and mark the number as positive */
		*str++ = '-';
		mpz_abs(&t, &t);

		/* subtract a char */
		--maxlen;
	}

	digs = 0;
	while (mp_iszero(&t) == MP_NO) {
		if (--maxlen < 1) {
			/* no more room */
			break;
		}
		if ((res = mp_div_d(&t, (mp_digit)radix, &t, &d)) != MP_OKAY) {
			mp_clear(&t);
			return res;
		}
		*str++ = mp_s_rmap[d];
		++digs;
	}

	/* reverse the digits of the string.  In this case _s points
	 * to the first digit [exluding the sign] of the number
	 */
	bn_reverse((unsigned char *)_s, digs);

	/* append a NULL so the string is properly terminated */
	*str = '\0';

	mp_clear(&t);
	return MP_OKAY;
}

#endif
