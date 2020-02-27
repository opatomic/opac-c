/*
 * Copyright 2018-2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include "tommathex.h"

#define SASSERT(e) switch(0){case 0: case e:;}

static void revdigs(char* s, size_t len) {
	char* e = s + len - 1;
	for (; s < e; ++s, --e) {
		char tmp = *s;
		*s = *e;
		*e = tmp;
	}
}

#ifdef OPA_USEGMP

mp_err mp_init(mp_int* a) {
	mpz_init(a);
	return MP_OKAY;
}

mp_err mp_init_copy(mp_int* a, const mp_int* b) {
	mpz_init_set(a, b);
	return MP_OKAY;
}

mp_err mp_copy(const mp_int* a, mp_int* b) {
	mpz_set(b, a);
	return MP_OKAY;
}

uint64_t mp_get_u64(const mp_int* a) {
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

void mp_set_u64(mp_int* a, uint64_t b) {
	SASSERT(sizeof(unsigned int) == 4);
	if (b <= UINT_MAX) {
		mpz_set_ui(a, (unsigned int)(b));
	} else {
		mpz_set_ui(a, (unsigned int)(b >> 32));
		mpz_mul_2exp(a, a, 32);
		mpz_add_ui(a, a, (unsigned int)b);
	}
}

mp_err mp_abs(const mp_int* a, mp_int* b) {
	mpz_abs(b, a);
	return MP_OKAY;
}

mp_err mp_add(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_add(c, a, b);
	return MP_OKAY;
}

mp_err mp_sub(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_sub(c, a, b);
	return MP_OKAY;
}

mp_err mp_mul(const mp_int* a, const mp_int* b, mp_int* c) {
	mpz_mul(c, a, b);
	return MP_OKAY;
}

mp_err mp_add_d(const mp_int* a, mp_digit b, mp_int* c) {
	mpz_add_ui(c, a, b);
	return MP_OKAY;
}

mp_err mp_mul_d(const mp_int* a, mp_digit b, mp_int* c) {
	mpz_mul_ui(c, a, b);
	return MP_OKAY;
}

mp_err mp_div_d(const mp_int* a, mp_digit b, mp_int* c, mp_digit* d) {
	*d = mpz_tdiv_q_ui(c, a, b);
	return MP_OKAY;
}

mp_err mp_unpack(mp_int* rop, size_t count, int order, size_t size, int endian, size_t nails, const void* op) {
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
 * Note: the following function is adapted from libtommath
 * https://github.com/libtom/libtommath/
 */
mp_err mp_to_radix(const mp_int *a, char *str, size_t maxlen, size_t *written, int radix) {
	static const char *const mp_s_rmap = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
	size_t  digs;
	mp_err  err;
	mp_int  t;
	mp_digit d;
	char   *_s = str;

	/* check range of radix and size*/
	if (maxlen < 2u) {
		return MP_BUF;
	}
	if ((radix < 2) || (radix > 64)) {
		return MP_VAL;
	}

	/* quick out if its zero */
	if (mp_iszero(a) == MP_YES) {
		*str++ = '0';
		*str = '\0';
		if (written != NULL) {
			*written = 2u;
		}
		return MP_OKAY;
	}

	if ((err = mp_init_copy(&t, a)) != MP_OKAY) {
		return err;
	}

	/* if it is negative output a - */
	if (mp_isneg(&t) == MP_YES) {
		/* we have to reverse our digits later... but not the - sign!! */
		++_s;

		/* store the flag and mark the number as positive */
		*str++ = '-';
		mp_abs(&t, &t);

		/* subtract a char */
		--maxlen;
	}
	digs = 0u;
	while (mp_iszero(&t) == MP_NO) {
		if (--maxlen < 1u) {
			/* no more room */
			err = MP_BUF;
			goto LBL_ERR;
		}
		if ((err = mp_div_d(&t, (mp_digit)radix, &t, &d)) != MP_OKAY) {
			goto LBL_ERR;
		}
		*str++ = mp_s_rmap[d];
		++digs;
	}
	/* reverse the digits of the string.  In this case _s points
	 * to the first digit [exluding the sign] of the number
	 */
	revdigs(_s, digs);

	/* append a NULL so the string is properly terminated */
	*str = '\0';
	digs++;

	if (written != NULL) {
		*written = (mp_isneg(a) == MP_YES) ? (digs + 1u): digs;
	}

	LBL_ERR:
	mp_clear(&t);
	return err;
}

#else

#define mpz_size(op) (op)->used
#define mpz_getlimbn(op, i) (op)->dp[i]

#endif

mp_err mp_to_decimal_n(const mp_int *a, char *str, size_t maxlen) {
	// note: this function assumes that mp_digit bit count is >= 7
#ifdef OPA_USEGMP
	//assert(mp_bits_per_limb >= 7);
#else
	SASSERT(MP_DIGIT_BIT >= 7);
#endif

	static const char* chars1 =
		"00000000001111111111222222222233333333334444444444"
		"55555555556666666666777777777788888888889999999999";
	static const char* chars2 =
		"01234567890123456789012345678901234567890123456789"
		"01234567890123456789012345678901234567890123456789";

	int      res;
	mp_int   t;
	mp_digit r;
	mp_digit v;
	char   *_s   = str;
	char   *stop = str + maxlen - 1;

	if (maxlen < 2) {
		return MP_VAL;
	}

	/* quick out if its zero */
	if (mp_iszero(a) == MP_YES) {
		*str++ = '0';
		*str = '\0';
		return MP_OKAY;
	}

	/* if it is negative output a - */
	if (mp_isneg(a)) {
		/* we have to reverse our digits later... but not the - sign!! */
		++_s;
		*str++ = '-';
	}

	if (mpz_size(a) > 1 && str < stop) {
		if ((res = mp_init_copy(&t, a)) != MP_OKAY) {
			return res;
		}
		if ((res = mp_abs(&t, &t)) != MP_OKAY) {
			mp_clear(&t);
			return res;
		}
		do {
			if ((res = mp_div_d(&t, 100, &t, &r)) != MP_OKAY) {
				mp_clear(&t);
				return res;
			}
			*str++ = chars2[r];
			if (str < stop) {
				*str++ = chars1[r];
			}
		} while (mpz_size(&t) > 1 && str < stop);
		v = mpz_getlimbn(&t, 0);
		mp_clear(&t);
	} else {
		v = mpz_getlimbn(a, 0);
	}

	while (v >= 100 && str < stop) {
		r = v % 100;
		v = v / 100;
		*str++ = chars2[r];
		if (str < stop) {
			*str++ = chars1[r];
		}
	}

	if (str < stop) {
		if (v >= 10) {
			*str++ = chars2[v];
			if (str < stop) {
				*str++ = chars1[v];
			}
		} else {
			*str++ = '0' + v;
		}
	}

	/* reverse the digits of the string.  In this case _s points
	 * to the first digit [excluding the sign] of the number
	 */
	revdigs(_s, str - _s);

	/* append a NULL so the string is properly terminated */
	*str = '\0';

	return MP_OKAY;
}
