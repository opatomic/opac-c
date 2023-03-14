/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <limits.h>
#include <string.h>

#include "opabigdec.h"
#include "opabigdec_private.h"
#include "opacore.h"

// some of this code is modeled after libtomfloat
// libtomfloat isn't used because it stores numbers in form significand*2^exponent rather than significand*10^exponent


#define OPABIGDEC_BIGENDIAN 1

#define OPABIGDEC_NEGINF -1
#define OPABIGDEC_POSINF 1


void opabigdecInit(opabigdec* a) {
	memset(a, 0, sizeof(opabigdec));
	opabigintInit(&a->significand);
	a->exponent = 0;
	a->inf = 0;
}

int opabigdecCopy(opabigdec* dst, const opabigdec* src) {
	if (src == dst) {
		return 0;
	}
	int err = opabigintCopy(&dst->significand, &src->significand);
	if (!err) {
		dst->exponent = src->exponent;
		dst->inf = src->inf;
	}
	return err;
}

void opabigdecFree(opabigdec* a) {
	opabigintFree(&a->significand);
	a->exponent = 0;
	a->inf = 0;
}

int opabigdecIsNeg(const opabigdec* a) {
	if (a->inf) {
		return a->inf == OPABIGDEC_NEGINF;
	}
	return opabigintIsNeg(&a->significand);
}

int opabigdecIsZero(const opabigdec* a) {
	if (a->inf) {
		return 0;
	}
	return opabigintIsZero(&a->significand);
}

int opabigdecIsFinite(const opabigdec* a) {
	return a->inf == 0;
}

int opabigdecNegate(opabigdec* dst, const opabigdec* src) {
	int err = opabigdecCopy(dst, src);
	if (!err) {
		if (dst->inf) {
			dst->inf = dst->inf == OPABIGDEC_NEGINF ? OPABIGDEC_POSINF : OPABIGDEC_NEGINF;
		} else {
			err = opabigintNegate(&dst->significand, &dst->significand);
		}
	}
	return err;
}

int opabigdecSet64(opabigdec* a, uint64_t val, int isNeg, int32_t exp) {
	int err = opabigintSetU64(&a->significand, val);
	if (isNeg && !err) {
		err = opabigintNegate(&a->significand, &a->significand);
	}
	if (!err) {
		a->exponent = exp;
		a->inf = 0;
	}
	return err;
}

int opabigdecGetMag64(const opabigdec* a, uint64_t* pVal) {
	if (a->inf) {
		return OPA_ERR_OVERFLOW;
	}
	// TODO: test this!
	if (a->exponent >= 0) {
		static const uint64_t MAX10 = (UINT64_MAX) / 10;
		if (opabigintCountBits(&a->significand) > 64) {
			return OPA_ERR_OVERFLOW;
		}
		int32_t exp;
		uint64_t val = opabigintGetMagU64(&a->significand);
		for (exp = a->exponent; exp > 0 && val <= MAX10; --exp) {
			val = val * 10;
		}
		if (exp > 0) {
			return OPA_ERR_OVERFLOW;
		}
		*pVal = val;
		return 0;
	} else {
		opabigint tmp;
		opabigintDigit rem = 0;
		int32_t exp;
		opabigintInit(&tmp);
		int err = opabigintCopy(&tmp, &a->significand);
		// TODO: can batch this into fewer calls by calling opabigintDivDig() with 10/100/1000/10000/etc
		for (exp = a->exponent; !err && exp < 0 && rem == 0; ++exp) {
			err = opabigintDivDig(&tmp, &rem, &tmp, 10);
		}
		if (!err && (rem || opabigintCountBits(&tmp) > 64)) {
			// TODO: OPA_ERR_OVERFLOW is a bad error code name to indicate remainder?
			err = OPA_ERR_OVERFLOW;
		}
		if (!err) {
			*pVal = opabigintGetMagU64(&tmp);
		}
		opabigintFree(&tmp);
		return err;
	}
}

int opabigdecExtend(opabigdec* v, uint32_t amount) {
	if (amount == 0) {
		return 0;
	}
	int err = 0;
	if (OPABIGINT_DIGIT_BITS >= 28) {
		// TODO: this is untested
		// can batch this into fewer calls by calling opabigintMulDig() with 10/100/1000/10000/etc
		static const opabigintDigit dig10a[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
		static const size_t dig10num = sizeof(dig10a) / sizeof(dig10a[0]);
		while (amount > 0 && !err) {
			int tens = amount > dig10num ? (int)dig10num : (int)amount;
			if (v->exponent <= INT32_MIN + tens) {
				// prevent overflow
				break;
			}
			err = opabigintMulDig(&v->significand, &v->significand, dig10a[tens - 1]);
			if (!err) {
				v->exponent -= tens;
				amount -= tens;
			}
		}
	}
	for (; amount > 0 && !err; --amount) {
		if (v->exponent == INT32_MIN) {
			return OPA_ERR_OVERFLOW;
		}
		err = opabigintMulDig(&v->significand, &v->significand, 10);
		if (!err) {
			--v->exponent;
		}
	}
	return err;
}

static int opabigdecSetInf(opabigdec* result, char infval) {
	OASSERT(infval == OPABIGDEC_NEGINF || infval == OPABIGDEC_POSINF);
	int err = opabigintZero(&result->significand);
	if (!err) {
		result->exponent = 0;
		result->inf = infval;
	}
	return err;
}

static int opabigdecAddInternal(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	OASSERT(a->exponent == b->exponent);
	int err = opabigintAdd(&result->significand, &a->significand, &b->significand);
	if (!err) {
		result->exponent = a->exponent;
	}
	return err;
}

int opabigdecAdd(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	if (a->inf || b->inf) {
		if (a->inf && b->inf && a->inf != b->inf) {
			return OPA_ERR_OVERFLOW;
		}
		return opabigdecSetInf(result, a->inf ? a->inf : b->inf);
	} else if (a->exponent == b->exponent) {
		return opabigdecAddInternal(result, a, b);
	} else if (a->exponent > b->exponent) {
		return opabigdecAdd(result, b, a);
	} else if (a == result || b == result) {
		opabigdec tmp;
		opabigdecInit(&tmp);
		int err = opabigdecAdd(&tmp, a, b);
		if (!err) {
			err = opabigdecCopy(result, &tmp);
		}
		opabigdecFree(&tmp);
		return err;
	} else {
		int err = opabigdecCopy(result, b);
		if (!err) {
			err = opabigdecExtend(result, b->exponent - a->exponent);
		}
		if (!err) {
			err = opabigdecAddInternal(result, a, result);
		}
		return err;
	}
}

static int opabigdecSubInternal(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	OASSERT(a->exponent == b->exponent);
	int err = opabigintSub(&result->significand, &a->significand, &b->significand);
	if (!err) {
		result->exponent = a->exponent;
	}
	return err;
}

int opabigdecSub(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	if (a->inf || b->inf) {
		if (a->inf && b->inf && a->inf == b->inf) {
			return OPA_ERR_OVERFLOW;
		}
		return opabigdecSetInf(result, a->inf ? a->inf : (b->inf == OPABIGDEC_NEGINF ? OPABIGDEC_POSINF : OPABIGDEC_NEGINF));
	} else if (a->exponent == b->exponent) {
		return opabigdecSubInternal(result, a, b);
	} else if (a == result || b == result) {
		opabigdec tmp;
		opabigdecInit(&tmp);
		int err = opabigdecSub(&tmp, a, b);
		if (!err) {
			err = opabigdecCopy(result, &tmp);
		}
		opabigdecFree(&tmp);
		return err;
	} else if (a->exponent > b->exponent) {
		int err = opabigdecCopy(result, a);
		if (!err) {
			err = opabigdecExtend(result, a->exponent - b->exponent);
		}
		if (!err) {
			err = opabigdecSubInternal(result, result, b);
		}
		return err;
	} else {
		int err = opabigdecCopy(result, b);
		if (!err) {
			err = opabigdecExtend(result, b->exponent - a->exponent);
		}
		if (!err) {
			err = opabigdecSubInternal(result, a, result);
		}
		return err;
	}
}

static int opabigdecMulInternal(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	OASSERT(a->exponent == b->exponent);
	int err = opabigintMul(&result->significand, &a->significand, &b->significand);
	if (!err) {
		result->exponent = a->exponent + b->exponent;
	}
	return err;
}

int opabigdecMul(opabigdec* result, const opabigdec* a, const opabigdec* b) {
	if (a->inf || b->inf) {
		if (opabigdecIsZero(a) || opabigdecIsZero(b)) {
			return opabigdecSet64(result, 0, 0, 0);
		} else {
			return opabigdecSetInf(result, opabigdecIsNeg(a) == opabigdecIsNeg(b) ? OPABIGDEC_POSINF : OPABIGDEC_NEGINF);
		}
	} else if (a->exponent == b->exponent) {
		return opabigdecMulInternal(result, a, b);
	} else if (a->exponent > b->exponent) {
		return opabigdecMul(result, b, a);
	} else if (a == result || b == result) {
		opabigdec tmp;
		opabigdecInit(&tmp);
		int err = opabigdecMul(&tmp, a, b);
		if (!err) {
			err = opabigdecCopy(result, &tmp);
		}
		opabigdecFree(&tmp);
		return err;
	} else {
		int err = opabigdecCopy(result, b);
		if (!err) {
			err = opabigdecExtend(result, b->exponent - a->exponent);
		}
		if (!err) {
			err = opabigdecMulInternal(result, a, result);
		}
		return err;
	}
}

static int opabigdecImport3(opabigdec* bd, const uint8_t* src, size_t numBytes, int isNeg, int isBigEndian, int32_t exponent) {
	if (!isBigEndian) {
		return OPA_ERR_INVARG;
	}
	int err = opabigintReadBytes(&bd->significand, src, numBytes);
	if (isNeg && !err) {
		err = opabigdecNegate(bd, bd);
	}
	if (!err) {
		bd->exponent = exponent;
	}
	return err;
}

static int opabigdecLoadVarint(opabigdec* bd, const uint8_t* so, int isNeg) {
	uint64_t val;
	int err = opaviLoadWithErr(so + 1, &val, NULL);
	if (!err) {
		err = opabigdecSet64(bd, val, isNeg, 0);
	}
	return err;
}

static int opabigdecLoadExponent(const uint8_t* pos, int isNegExp, int32_t* pExponent, const uint8_t** pPos) {
	uint64_t exp;
	int err = opaviLoadWithErr(pos, &exp, pPos);
	if (!err) {
		if ((isNegExp && exp > ((uint64_t)INT32_MAX + 1)) || (!isNegExp && exp > ((uint64_t)INT32_MAX))) {
			err = OPA_ERR_INVARG;
		}
	}
	if (!err) {
		*pExponent = isNegExp ? ((int32_t)0 - (int32_t)exp) : (int32_t)exp;
	}
	return err;
}

static int opabigdecLoadBigInt(opabigdec* dst, const uint8_t* src, int isNeg, int32_t exponent) {
	uint64_t numBytes;
	int err = opaviLoadWithErr(src, &numBytes, &src);
	if (!err && numBytes > SIZE_MAX) {
		err = OPA_ERR_INVARG;
	}
	if (!err) {
		err = opabigdecImport3(dst, src, numBytes, isNeg, OPABIGDEC_BIGENDIAN, exponent);
	}
	return err;
}

static int opabigdecLoadVarDec(opabigdec* bd, const uint8_t* so, int isNegExp, int isNegMan) {
	uint64_t man = 0;
	int32_t exp;
	int err = opabigdecLoadExponent(so + 1, isNegExp, &exp, &so);
	if (!err) {
		err = opaviLoadWithErr(so, &man, NULL);
	}
	if (!err) {
		err = opabigdecSet64(bd, man, isNegMan, exp);
	}
	return err;
}

static int opabigdecLoadBigDec(opabigdec* dst, const uint8_t* src, int isNegExp, int isNegMan) {
	int32_t exp;
	int err = opabigdecLoadExponent(src + 1, isNegExp, &exp, &src);
	if (!err) {
		err = opabigdecLoadBigInt(dst, src, isNegMan, exp);
	}
	return err;
}

int opabigdecLoadSO(opabigdec* bd, const uint8_t* so) {
	switch (*so) {
		case OPADEF_NEGINF:    return opabigdecSetInf(bd, OPABIGDEC_NEGINF);
		case OPADEF_POSINF:    return opabigdecSetInf(bd, OPABIGDEC_POSINF);

		case OPADEF_ZERO:      return opabigdecSet64(bd, 0, 0, 0);

		case OPADEF_POSVARINT: return opabigdecLoadVarint(bd, so, 0);
		case OPADEF_NEGVARINT: return opabigdecLoadVarint(bd, so, 1);

		case OPADEF_POSBIGINT: return opabigdecLoadBigInt(bd, so + 1, 0, 0);
		case OPADEF_NEGBIGINT: return opabigdecLoadBigInt(bd, so + 1, 1, 0);

		case OPADEF_POSPOSVARDEC: return opabigdecLoadVarDec(bd, so, 0, 0);
		case OPADEF_POSNEGVARDEC: return opabigdecLoadVarDec(bd, so, 0, 1);
		case OPADEF_NEGPOSVARDEC: return opabigdecLoadVarDec(bd, so, 1, 0);
		case OPADEF_NEGNEGVARDEC: return opabigdecLoadVarDec(bd, so, 1, 1);

		case OPADEF_POSPOSBIGDEC: return opabigdecLoadBigDec(bd, so, 0, 0);
		case OPADEF_POSNEGBIGDEC: return opabigdecLoadBigDec(bd, so, 0, 1);
		case OPADEF_NEGPOSBIGDEC: return opabigdecLoadBigDec(bd, so, 1, 0);
		case OPADEF_NEGNEGBIGDEC: return opabigdecLoadBigDec(bd, so, 1, 1);

		default: return OPA_ERR_INVARG;
	}
}

static size_t opabigdecNumBytesForBigInt(const opabigdec* a, uint8_t bytesPerWord) {
	size_t bits = opabigintCountBits(&a->significand);
	size_t numWords = (bits / (bytesPerWord * 8)) + (((bits % (bytesPerWord * 8)) != 0) ? 1 : 0);
	return numWords * bytesPerWord;
}

static size_t opabigdecExport3(const opabigdec* bd, uint8_t useBigEndian, uint8_t* buff, size_t buffLen) {
	//OASSERT(bytesPerWord > 0 && bytesPerWord <= 8);
	//int endian = useBigEndian ? 1 : -1;
	//int tomerr = mp_export(dst, NULL, endian, bytesPerWord, endian, 0, &bd->significand);
	//return tomerr ? opabigdecConvertErr(tomerr) : 0;

	size_t lenReq = opabigdecNumBytesForBigInt(bd, 1);
	if (buffLen >= lenReq) {
		opabigintWriteBytes(&bd->significand, useBigEndian, buff, lenReq);
	}
	return lenReq;
}

static size_t opabigdecSaveBigInt(const opabigdec* val, uint8_t* buff, size_t buffLen) {
	size_t numBytes = opabigdecNumBytesForBigInt(val, 1);
	size_t hlen = opaviStoreLen(numBytes);
	if (buffLen >= hlen + numBytes) {
		buff = opaviStore(numBytes, buff);
		opabigdecExport3(val, OPABIGDEC_BIGENDIAN, buff, buffLen - hlen);
	}
	return hlen + numBytes;
}

size_t opabigdecStoreSO(const opabigdec* val, uint8_t* buff, size_t buffLen) {
	// note: number could be stored as decimal but not need to be serialized as decimal. ie, 1000e-3=1 12e2=1200
	if (val->inf) {
		if (buffLen > 0) {
			*buff = val->inf == OPABIGDEC_NEGINF ? OPADEF_NEGINF : OPADEF_POSINF;
		}
		return 1;
	}

	if (val->exponent == 0 && opabigdecIsZero(val)) {
		if (buffLen > 0) {
			*buff = OPADEF_ZERO;
		}
		return 1;
	}

	if (opabigintCountBits(&val->significand) < 64) {
		uint64_t val64 = opabigintGetMagU64(&val->significand);
		if (val->exponent == 0) {
			// varint
			size_t lenNeeded = 1 + opaviStoreLen(val64);
			if (buffLen >= lenNeeded) {
				buff[0] = opabigdecIsNeg(val) ? OPADEF_NEGVARINT : OPADEF_POSVARINT;
				opaviStore(val64, buff + 1);
			}
			return lenNeeded;
		} else {
			// vardec
			uint32_t absExp = val->exponent < 0 ? 0 - val->exponent : val->exponent;
			size_t lenNeeded = 1 + opaviStoreLen(absExp) + opaviStoreLen(val64);
			if (buffLen >= lenNeeded) {
				buff[0] = opabigdecIsNeg(val) ? (val->exponent < 0 ? OPADEF_NEGNEGVARDEC : OPADEF_POSNEGVARDEC) : (val->exponent < 0 ? OPADEF_NEGPOSVARDEC : OPADEF_POSPOSVARDEC);
				buff = opaviStore(absExp, buff + 1);
				opaviStore(val64, buff);
			}
			return lenNeeded;
		}
	} else if (val->exponent == 0) {
		// bigint
		if (buffLen > 0) {
			*buff++ = opabigdecIsNeg(val) ? OPADEF_NEGBIGINT : OPADEF_POSBIGINT;
			--buffLen;
		}
		return 1 + opabigdecSaveBigInt(val, buff, buffLen);
	} else {
		// bigdec
		uint32_t absExp = (val->exponent < 0) ? (0 - val->exponent) : val->exponent;
		size_t lenReq = 1 + opaviStoreLen(absExp);
		if (buffLen >= lenReq) {
			buff[0] = opabigdecIsNeg(val) ? (val->exponent < 0 ? OPADEF_NEGNEGBIGDEC : OPADEF_POSNEGBIGDEC) : (val->exponent < 0 ? OPADEF_NEGPOSBIGDEC : OPADEF_POSPOSBIGDEC);
			buff = opaviStore(absExp, buff + 1);
			buffLen -= lenReq;
		} else {
			buffLen = 0;
		}
		return lenReq + opabigdecSaveBigInt(val, buff, buffLen);
	}
}

int opabigdecFromStr(opabigdec* v, const char* str, const char* end, int radix) {
	if (radix < 2 || radix > 10) {
		// only support up to base 10 for now. cannot mix hex chars with e/E exponent separator
		return OPA_ERR_INVARG;
	}

	int infval = opaIsInfStr(str, end - str);
	if (infval) {
		return opabigdecSetInf(v, infval < 0 ? OPABIGDEC_NEGINF : OPABIGDEC_POSINF);
	}

	int err;
	int neg;
	const char* decPos = NULL;
	ptrdiff_t decLen = 0;

	err = opabigintZero(&v->significand);
	if (err) {
		return err;
	}

	if (str < end && *str == '-') {
		++str;
		neg = 1;
	} else {
		neg = 0;
	}

	while (1) {
		for (; str < end && *str >= '0' && *str <= '9'; ++str) {
			if ((err = opabigintMulDig(&v->significand, &v->significand, radix)) != 0) {
				return err;
			}
			if ((err = opabigintAddDig(&v->significand, &v->significand, (unsigned int) (*str - '0'))) != 0) {
				return err;
			}
		}
		// TODO: handle other locale characters? might use , rather than . ??
		if (str < end && *str == '.' && decPos == NULL) {
			decPos = ++str;
			continue;
		}
		break;
	}

	if (decPos != NULL) {
		decLen = str - decPos;
	}

	if (str < end && (*str == 'e' || *str == 'E')) {
		++str;
		int negExp = 0;
		if (str < end) {
			if (*str == '-') {
				negExp = 1;
				++str;
			} else if (*str == '+') {
				++str;
			}
		}
		uint64_t currVal = 0;
		for (; str < end && *str >= '0' && *str <= '9'; ++str) {
			currVal = (currVal * radix) + (*str - '0');
			if ((negExp && currVal > (uint64_t)INT32_MAX + 1) || (!negExp && currVal > INT32_MAX)) {
				return OPA_ERR_OVERFLOW;
			}
		}
		v->exponent = (int32_t) (negExp ? ((int64_t)0 - currVal) : currVal);
	} else {
		v->exponent = 0;
	}

	if (decLen != 0) {
		// TODO: check for overflow
		//   https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html
		int64_t newVal = v->exponent - decLen;
		if (newVal < INT32_MIN || newVal > INT32_MAX) {
			return OPA_ERR_OVERFLOW;
		}
		v->exponent = (int32_t) newVal;
	}

	if (neg && !err) {
		err = opabigdecNegate(v, v);
	}

	return err;
}

static size_t opabigdecCharsPerBit(size_t bits, int radix) {
	// this is just an approximation
	size_t chars;
	if (radix < 4) {
		chars = bits;
	} else if (radix < 8) {
		chars = bits / 2;
	} else if (radix < 16) {
		chars = bits / 3;
	} else if (radix < 32) {
		chars = bits / 4;
	} else if (radix < 64) {
		chars = bits / 5;
	} else {
		chars = bits / 6;
	}
	// add an extra char in case division (above) caused a round down to floor of result
	++chars;
	return chars;
}

size_t opabigdecMaxStringLen(const opabigdec* a, int radix) {
	if (a->inf) {
		return a->inf == OPABIGDEC_NEGINF ? 5 : 4;
	}
	// TODO: use mp_radix_size()? it uses division so will be slower but is exact. code currently gives approximation
	size_t chars = opabigdecCharsPerBit(opabigintCountBits(&a->significand), radix);

	if (opabigintIsNeg(&a->significand)) {
		// extra char for negative sign
		++chars;
	}
	if (a->exponent != 0) {
		// 2 extra chars for "E+" or "E-"
		chars += 2;
		int32_t currExp = a->exponent;
		do {
			++chars;
			currExp = currExp / radix;
		} while (currExp != 0);
		// account for possible prefix "0." + 0's or just "."
		chars += 2 + 5;
	}
	// extra char for null
	++chars;

	return chars;
}

static void opabigdecReverseStr(char* s, size_t len) {
	char* end = s + len - 1;
	for (; s < end; ++s, --end) {
		char tmp = *s;
		*s = *end;
		*end = tmp;
	}
}

static size_t opabigDecExpToRadix(uint64_t exp, char radix, char* str) {
	char* pos = str;
	do {
		char digit = exp % radix;
		exp = exp / radix;
		*pos++ = '0' + digit;
	} while (exp > 0);

	opabigdecReverseStr(str, pos - str);
	*pos = 0;

	return pos - str;
}

int opabigdecToString(const opabigdec* a, char* str, size_t space, size_t* pWritten, int radix) {
	if (radix < 2 || radix > 10 || space <= 1) {
		// only support up to base 10 for now. cannot mix hex chars with e/E exponent separator
		// TODO: use 'p' instead of 'e' for hex string?
		return OPA_ERR_INVARG;
	}

	if (a->inf) {
		const char* infStr = a->inf == OPABIGDEC_NEGINF ? "-inf" : "inf";
		size_t infStrLen = a->inf == OPABIGDEC_NEGINF ? 5 : 4;
		if (str != NULL) {
			if (space < infStrLen) {
				return OPA_ERR_INVARG;
			}
			memcpy(str, infStr, infStrLen);
		}
		if (pWritten != NULL) {
			*pWritten = infStrLen;
		}
		return 0;
	}

	size_t totUsedBytes;
	int err = opabigintToRadix(&a->significand, str, space, &totUsedBytes, radix);

	if (!err && a->exponent != 0) {
		size_t digitsLen; // number of digit bytes, not including null char
		char* digitsStart = str;
		if (opabigdecIsNeg(a)) {
			OASSERT(totUsedBytes > 1);
			digitsLen = totUsedBytes - 2;
			if (str != NULL) {
				// skip past the neg sign
				++digitsStart;
			}
		} else {
			OASSERT(totUsedBytes > 0);
			digitsLen = totUsedBytes - 1;
		}

		int64_t e = a->exponent;
		int64_t adjustedExp = e + digitsLen - 1;
		if (e < 0 && adjustedExp >= -6) {
			// convert to character form without exponent
			if (adjustedExp >= 0) {
				if (space < totUsedBytes + 1) {
					return OPA_ERR_INVARG;
				}
				if (str != NULL) {
					char* insertPos = digitsStart + digitsLen + e;
					memmove(insertPos + 1, insertPos, str + totUsedBytes - insertPos);
					*insertPos = '.';
				}
				++totUsedBytes;
			} else {
				size_t prefixZs = 0 - adjustedExp - 1;
				// 2 bytes for "0."; prefixZs bytes for 0's after '.'; 1 byte for null at end (digitsLen does not include null)
				if (space < digitsLen + 2 + prefixZs + 1) {
					return OPA_ERR_INVARG;
				}
				totUsedBytes += 2 + prefixZs;
				if (str != NULL) {
					memmove(digitsStart + 2 + prefixZs, digitsStart, digitsLen + 1);
					*digitsStart++ = '0';
					*digitsStart++ = '.';
					for (; prefixZs > 0; --prefixZs) {
						*digitsStart++ = '0';
					}
				}
			}
			e = 0;
		} else {
			// fall back to exponential notation
			e = adjustedExp;
			if (digitsLen > 1) {
				if (space < totUsedBytes + 1) {
					return OPA_ERR_INVARG;
				}
				if (str != NULL) {
					// insert '.' after 1st digit
					char* insertPos = digitsStart + 1;
					memmove(insertPos + 1, insertPos, str + totUsedBytes - insertPos);
					*insertPos = '.';
				}
				++totUsedBytes;
			}
		}
		if (e != 0) {
			char tmpBuff[24];
			tmpBuff[0] = 'E';
			tmpBuff[1] = e < 0 ? '-' : '+';
			size_t expLen = opabigDecExpToRadix(e < 0 ? ((int64_t)0) - e : e, radix, tmpBuff + 2) + 2;
			OASSERT(tmpBuff[expLen] == 0);
			if (space < totUsedBytes + expLen) {
				return OPA_ERR_INVARG;
			}
			if (str != NULL) {
				memcpy(str + totUsedBytes - 1, tmpBuff, expLen + 1);
			}
			totUsedBytes += expLen;
		}
	}

	if (pWritten != NULL && !err) {
		*pWritten = totUsedBytes;
	}

#ifdef OPADBG
	if (!err) {
		OASSERT(str == NULL || totUsedBytes == strlen(str) + 1);
		OASSERT(pWritten == NULL || opabigdecMaxStringLen(a, radix) >= *pWritten);
	}
#endif

	return err;
}
