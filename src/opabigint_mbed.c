/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef OPABIGINT_USE_MBED

#include <string.h>

#include "opabigint.h"
#include "opacore.h"

static int converr(int mbedErr) {
	if (!mbedErr) {
		return 0;
	}
	switch (mbedErr) {
		//TODO: add MBEDTLS_ERR_MPI_FILE_IO_ERROR?

		case MBEDTLS_ERR_MPI_BAD_INPUT_DATA:
		case MBEDTLS_ERR_MPI_INVALID_CHARACTER:
		case MBEDTLS_ERR_MPI_BUFFER_TOO_SMALL:
		case MBEDTLS_ERR_MPI_NEGATIVE_VALUE:
		case MBEDTLS_ERR_MPI_DIVISION_BY_ZERO:
		case MBEDTLS_ERR_MPI_NOT_ACCEPTABLE:
			return OPA_ERR_INVARG;
		case MBEDTLS_ERR_MPI_ALLOC_FAILED:
			return OPA_ERR_NOMEM;
	}
	return OPA_ERR_INTERNAL;
}

void opabigintInit(opabigint* a) {
	mbedtls_mpi_init(a);
}

void opabigintFree(opabigint* a) {
	mbedtls_mpi_free(a);
}

int opabigintIsZero(const opabigint* a) {
	return mbedtls_mpi_cmp_int(a, 0) == 0;
}

int opabigintIsNeg(const opabigint* a) {
	return mbedtls_mpi_cmp_int(a, 0) < 0;
}

int opabigintIsEven(const opabigint* a) {
	return mbedtls_mpi_get_bit(a, 0) == 0;
}

uint64_t opabigintGetMagU64(const opabigint* a) {
	SASSERT(sizeof(mbedtls_mpi_uint) >= 8 || sizeof(mbedtls_mpi_uint) == 4);
	if (sizeof(mbedtls_mpi_uint) >= 8) {
		if (a->n > 0) {
			return a->p[0];
		} else {
			return 0;
		}
	} else if (sizeof(mbedtls_mpi_uint) == 4) {
		if (a->n > 1) {
			// TODO: this needs tested
			uint64_t val = a->p[1];
			val = val << 32;
			val = val | a->p[0];
			return val;
		} else if (a->n > 0) {
			return a->p[0];
		} else {
			return 0;
		}
	} else {
		OPAPANICF("unsupported size %" OPA_FMT_ZU " for mbedtls_mpi_uint", sizeof(mbedtls_mpi_uint));
	}
}

size_t opabigintCountBits(const opabigint* a) {
	return mbedtls_mpi_bitlen(a);
}

int opabigintCompareMag(const opabigint* a, const opabigint* b) {
	return mbedtls_mpi_cmp_abs(a, b);
}

size_t opabigintUsedLimbs(const opabigint* a) {
	size_t len = a->n;
	while (len > 0 && a->p[len - 1] == 0) {
		--len;
	}
	return len;
}

opabigintDigit opabigintGetLimb(const opabigint* a, size_t n) {
	if (n < a->n) {
		return a->p[n];
	}
	return 0;
}

int opabigintEnsureSpaceForCopy(opabigint* dst, const opabigint* src) {
	return converr(mbedtls_mpi_grow(dst, opabigintUsedLimbs(src)));
}

int opabigintCopy(opabigint* dst, const opabigint* src) {
	return converr(mbedtls_mpi_copy(dst, src));
}

int opabigintAbs(opabigint* dst, const opabigint* src) {
	int err = opabigintCopy(dst, src);
	if (!err) {
		OASSERT(dst->s == -1 || dst->s == 1);
		if (dst->s < 0) {
			dst->s = 1;
		}
	}
	return err;
}

int opabigintNegate(opabigint* dst, const opabigint* src) {
	int err = opabigintCopy(dst, src);
	if (!err && !opabigintIsZero(dst)) {
		OASSERT(dst->s == -1 || dst->s == 1);
		dst->s = dst->s < 0 ? 1 : -1;
	}
	return err;
}

int opabigintZero(opabigint* a) {
	return converr(mbedtls_mpi_lset(a, 0));
}

int opabigintSetU64(opabigint* a, uint64_t val) {
	SASSERT(sizeof(mbedtls_mpi_uint) >= 8 || sizeof(mbedtls_mpi_uint) == 4);
	if (sizeof(mbedtls_mpi_uint) >= 8) {
		mbedtls_mpi_uint tmpLimbs[1] = {val};
		mbedtls_mpi tmpInt = {.s = 1, .n = 1, .p = tmpLimbs};
		return opabigintCopy(a, &tmpInt);
	} else if (sizeof(mbedtls_mpi_uint) == 4) {
		// TODO: test this!
		mbedtls_mpi_uint tmpLimbs[2] = {val & 0xFFFFFFFF, val >> 32};
		mbedtls_mpi tmpInt = {.s = 1, .n = 2, .p = tmpLimbs};
		return opabigintCopy(a, &tmpInt);
	} else {
		OPAPANICF("unsupported size %" OPA_FMT_ZU " for mbedtls_mpi_uint", sizeof(mbedtls_mpi_uint));
	}

	//val = htobe64(val);
	//return converr(mbedtls_mpi_read_binary(a, &val, sizeof(val)));
}

int opabigintAdd(opabigint* res, const opabigint* a, const opabigint* b) {
	return converr(mbedtls_mpi_add_mpi(res, a, b));
}

int opabigintSub(opabigint* res, const opabigint* a, const opabigint* b) {
	return converr(mbedtls_mpi_sub_mpi(res, a, b));
}

int opabigintMul(opabigint* res, const opabigint* a, const opabigint* b) {
	return converr(mbedtls_mpi_mul_mpi(res, a, b));
}

int opabigintAddDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	mbedtls_mpi_uint tmpLimbs[1] = {b};
	mbedtls_mpi tmpInt = {.s = 1, .n = 1, .p = tmpLimbs};
	return converr(mbedtls_mpi_add_mpi(res, a, &tmpInt));
}

int opabigintMulDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	return converr(mbedtls_mpi_mul_int(res, a, b));
}

int opabigintDivDig(opabigint* q, opabigintDigit* r, const opabigint* a, opabigintDigit b) {
	if (q == a) {
		// it's not documented (yet) but mbedtls_mpi_div_mpi() requires q/a to be different
		//    https://github.com/ARMmbed/mbedtls/issues/1105
		mbedtls_mpi tmpQ;
		mbedtls_mpi_init(&tmpQ);
		int err = opabigintDivDig(&tmpQ, r, a, b);
		if (!err) {
			err = opabigintCopy(q, &tmpQ);
		}
		mbedtls_mpi_free(&tmpQ);
		return err;
	}
	mbedtls_mpi_uint tmpLimbs[1] = {b};
	mbedtls_mpi tmpInt = {.s = 1, .n = 1, .p = tmpLimbs};
	if (r == NULL) {
		return converr(mbedtls_mpi_div_mpi(q, NULL, a, &tmpInt));
	} else {
		// TODO: should not need to init/free temp variable because remainder should only be 1 opabigintDigit long.
		//   therefore, can use stack for opabigintDigit array. however, not sure if underlying implementation will
		//   try to grow array for some reason.
		mbedtls_mpi tmpInt2;
		mbedtls_mpi_init(&tmpInt2);
		int err = converr(mbedtls_mpi_div_mpi(q, &tmpInt2, a, &tmpInt));
		if (!err) {
			OASSERT(mbedtls_mpi_bitlen(&tmpInt2) <= sizeof(mbedtls_mpi_uint) * 8);
			*r = opabigintGetLimb(&tmpInt2, 0);
		}
		mbedtls_mpi_free(&tmpInt2);
		return err;
	}
}

int opabigintReadBytes(opabigint* a, const unsigned char* buff, size_t buffLen) {
	return converr(mbedtls_mpi_read_binary(a, buff, buffLen));
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif
