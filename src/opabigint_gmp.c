/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef OPABIGINT_USE_GMP

#include "opabigint.h"
#include "opacore.h"

void opabigintInit(opabigint* a) {
	mpz_init(a);
}

void opabigintFree(opabigint* a) {
	mpz_clear(a);
}

int opabigintIsZero(const opabigint* a) {
	return mpz_sgn(a) == 0;
}

int opabigintIsNeg(const opabigint* a) {
	return mpz_sgn(a) < 0;
}

int opabigintIsEven(const opabigint* a) {
	return mpz_even_p(a);
}

uint64_t opabigintGetMagU64(const opabigint* a) {
	if (opabigintCountBits(a) > 64) {
		return 0;
	}
	if (sizeof(unsigned long int) >= sizeof(uint64_t)) {
		return (uint64_t) mpz_get_ui(a);
	}
	uint64_t val = 0;
	mpz_export(&val, NULL, 1, sizeof(val), 0, 0, a);
	return val;
}

size_t opabigintCountBits(const opabigint* a) {
	return mpz_sizeinbase(a, 2);
}

int opabigintCompareMag(const opabigint* a, const opabigint* b) {
	return mpz_cmpabs(a, b);
}

size_t opabigintUsedLimbs(const opabigint* a) {
	return mpz_size(a);
}

opabigintDigit opabigintGetLimb(const opabigint* a, size_t n) {
	return mpz_getlimbn(a, n);
}

int opabigintEnsureSpaceForCopy(opabigint* dst, const opabigint* src) {
	UNUSED(dst);
	UNUSED(src);
	// note: this does nothing because mpz_realloc() or mpz_realloc2() will set the value to 0 if it needs to grow.
	//   gmp will try to realloc when needed and cause the app to fail/crash if reallocation fails
	return 0;
}

int opabigintCopy(opabigint* dst, const opabigint* src) {
	mpz_set(dst, src);
	return 0;
}

int opabigintAbs(opabigint* dst, const opabigint* src) {
	mpz_abs(dst, src);
	return 0;
}

int opabigintNegate(opabigint* dst, const opabigint* src) {
	mpz_neg(dst, src);
	return 0;
}

int opabigintZero(opabigint* a) {
	return opabigintSetU64(a, 0);
}

int opabigintSetU64(opabigint* a, uint64_t val) {
	/*
	SASSERT(sizeof(unsigned long int) >= sizeof(uint64_t) || sizeof(unsigned long int) == 4);
	if (sizeof(unsigned long int) >= sizeof(uint64_t)) {
		mpz_set_ui(a, val);
	} else if (sizeof(unsigned long int) == 4) {
		mpz_set_ui(a, (unsigned long int) (val >> 32));
		mpz_mul_2exp(a, a, 32);
		mpz_add_ui(a, a, (unsigned long int) (val & 0xFFFFFFFF));
	} else {
		OPAPANICF("unsupported size %zu for unsigned long int", sizeof(unsigned long int));
	}
	return 0;
	*/

	mpz_import(a, 1, 1, sizeof(val), 0, 0, &val);
	return 0;
}

int opabigintAdd(opabigint* res, const opabigint* a, const opabigint* b) {
	mpz_add(res, a, b);
	return 0;
}

int opabigintSub(opabigint* res, const opabigint* a, const opabigint* b) {
	mpz_sub(res, a, b);
	return 0;
}

int opabigintMul(opabigint* res, const opabigint* a, const opabigint* b) {
	mpz_mul(res, a, b);
	return 0;
}

int opabigintAddDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	SASSERT(sizeof(unsigned long int) >= sizeof(opabigintDigit));
	mpz_add_ui(res, a, b);
	return 0;
}

int opabigintMulDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	SASSERT(sizeof(unsigned long int) >= sizeof(opabigintDigit));
	mpz_mul_ui(res, a, b);
	return 0;
}

int opabigintDivDig(opabigint* q, opabigintDigit* r, const opabigint* a, opabigintDigit b) {
	SASSERT(sizeof(unsigned long int) >= sizeof(opabigintDigit));
	if (q != NULL) {
		opabigintDigit rem = mpz_tdiv_q_ui(q, a, b);
		if (r != NULL) {
			*r = rem;
		}
	} else {
		opabigintDigit rem = mpz_tdiv_ui(a, b);
		if (r != NULL) {
			*r = rem;
		}
	}
	return 0;
}

int opabigintReadBytes(opabigint* a, const unsigned char* buff, size_t buffLen) {
	mpz_import(a, buffLen, 1, 1, 1, 0, buff);
	return 0;
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif
