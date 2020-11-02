/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef OPABIGINT_USE_LTM

#include <string.h>

#include "opabigint.h"
#include "opacore.h"

// this macro determines whether the specified mp_int has been successfully initialized by mp_init()
//   an mp_int may not be initialized (because opabigintInit() cannot return error)
#define ISINITD(a) ((a)->dp != NULL)

static int converr(mp_err tomerr) {
	switch (tomerr) {
		case MP_OKAY: return 0;
		case MP_MEM:  return OPA_ERR_NOMEM;
		case MP_BUF:
		case MP_VAL:  return OPA_ERR_INVARG;
		default:
			OPALOGERRF("unknown libtom err %d", tomerr);
			return OPA_ERR_INTERNAL;
	}
}

static int ensureInit(opabigint* a) {
	if (!ISINITD(a)) {
		return converr(mp_init(a));
	}
	return 0;
}

void opabigintInit(opabigint* a) {
	if (mp_init(a) != MP_OKAY) {
		// if error occurs, cannot return error code! set a->dp to NULL to indicate it's not initialized
		memset(a, 0, sizeof(mp_int));
		a->dp = NULL;
	}
}

void opabigintFree(opabigint* a) {
	if (ISINITD(a)) {
		mp_clear(a);
	}
}

int opabigintIsZero(const opabigint* a) {
	if (ISINITD(a)) {
		return mp_iszero(a) == MP_YES;
	}
	return 1;
}

int opabigintIsNeg(const opabigint* a) {
	if (ISINITD(a)) {
		return mp_isneg(a) == MP_YES;
	}
	return 0;
}

int opabigintIsEven(const opabigint* a) {
	if (ISINITD(a)) {
		return mp_iseven(a) == MP_YES;
	}
	return 1;
}

uint64_t opabigintGetMagU64(const opabigint* a) {
	if (ISINITD(a)) {
		return mp_get_mag_u64(a);
	}
	return 0;
}

size_t opabigintCountBits(const opabigint* a) {
	if (ISINITD(a)) {
		return mp_count_bits(a);
	}
	return 0;
}

int opabigintCompareMag(const opabigint* a, const opabigint* b) {
	if (ISINITD(a)) {
		if (ISINITD(b)) {
			return mp_cmp_mag(a, b);
		} else {
			// b is 0
			return mp_iszero(a) ? 0 : 1;
		}
	} else if (ISINITD(b)) {
		// a is 0
		return mp_iszero(b) ? 0 : -1;
	} else {
		// a and b are 0
		return 0;
	}
}

size_t opabigintUsedLimbs(const opabigint* a) {
	if (ISINITD(a)) {
		return a->used;
	}
	return 0;
}

opabigintDigit opabigintGetLimb(const opabigint* a, size_t n) {
	if (ISINITD(a) && a->used > 0 && n < (size_t)a->used) {
		return a->dp[n];
	}
	return 0;
}

int opabigintEnsureSpaceForCopy(opabigint* dst, const opabigint* src) {
	int err = ensureInit(dst);
	if (!err) {
		err = converr(mp_grow(dst, ISINITD(src) ? src->used : 0));
	}
	return err;
}

int opabigintCopy(opabigint* dst, const opabigint* src) {
	int err = ensureInit(dst);
	if (!err) {
		if (ISINITD(src)) {
			err = converr(mp_copy(src, dst));
		} else {
			mp_zero(dst);
		}
	}
	return err;
}

int opabigintAbs(opabigint* dst, const opabigint* src) {
	int err = ensureInit(dst);
	if (!err) {
		if (ISINITD(src)) {
			err = converr(mp_abs(src, dst));
		} else {
			mp_zero(dst);
		}
	}
	return err;
}

int opabigintNegate(opabigint* dst, const opabigint* src) {
	int err = ensureInit(dst);
	if (!err) {
		if (ISINITD(src)) {
			err = converr(mp_neg(src, dst));
		} else {
			mp_zero(dst);
		}
	}
	return err;
}

int opabigintZero(opabigint* a) {
	int err = ensureInit(a);
	if (!err) {
		mp_zero(a);
	}
	return err;
}

int opabigintSetU64(opabigint* a, uint64_t val) {
	int err = ensureInit(a);
	if (!err) {
		mp_set_u64(a, val);
	}
	return err;
}

int opabigintAdd(opabigint* res, const opabigint* a, const opabigint* b) {
	int err = ensureInit(res);
	if (!err) {
		if (!ISINITD(a)) {
			err = opabigintCopy(res, b);
		} else if (!ISINITD(b)) {
			err = opabigintCopy(res, a);
		} else {
			err = converr(mp_add(a, b, res));
		}
	}
	return err;
}

int opabigintSub(opabigint* res, const opabigint* a, const opabigint* b) {
	int err = ensureInit(res);
	if (!err) {
		if (ISINITD(a)) {
			if (ISINITD(b)) {
				err = converr(mp_sub(a, b, res));
			} else {
				err = opabigintCopy(res, a);
			}
		} else {
			err = opabigintNegate(res, b);
		}
	}
	return err;
}

int opabigintMul(opabigint* res, const opabigint* a, const opabigint* b) {
	int err = ensureInit(res);
	if (!err) {
		if (ISINITD(a) && ISINITD(b)) {
			err = converr(mp_mul(a, b, res));
		} else {
			err = opabigintZero(res);
		}
	}
	return err;
}

int opabigintAddDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	int err = ensureInit(res);
	if (!err) {
		if (ISINITD(a)) {
			err = converr(mp_add_d(a, b, res));
		} else {
			err = opabigintZero(res);
			if (!err) {
				err = opabigintAddDig(res, res, b);
			}
		}
	}
	return err;
}

int opabigintMulDig(opabigint* res, const opabigint* a, opabigintDigit b) {
	int err = ensureInit(res);
	if (!err) {
		if (ISINITD(a)) {
			err = converr(mp_mul_d(a, b, res));
		} else {
			err = opabigintZero(res);
		}
	}
	return err;
}

int opabigintDivDig(opabigint* q, opabigintDigit* r, const opabigint* a, opabigintDigit b) {
	if (b == 0) {
		return OPA_ERR_INVARG;
	}
	int err = q == NULL ? 0 : ensureInit(q);
	if (!err) {
		if (ISINITD(a)) {
			err = converr(mp_div_d(a, b, q, r));
		} else {
			err = opabigintZero(q);
			if (r != NULL) {
				*r = 0;
			}
		}
	}
	return err;
}

int opabigintReadBytes(opabigint* a, const unsigned char* buff, size_t buffLen) {
	int err = ensureInit(a);
	if (!err) {
		err = converr(mp_unpack(a, buffLen, MP_BIG_ENDIAN, 1, MP_BIG_ENDIAN, 0, buff));
	}
	return err;
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif
