/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <string.h>

#include "opabigint.h"
#include "opacore.h"

static void revdigs(char* s, size_t len) {
	char* e = s + len - 1;
	for (; s < e; ++s, --e) {
		char tmp = *s;
		*s = *e;
		*e = tmp;
	}
}

static int opabigintToRadix10(const opabigint* a, char* str, size_t space, size_t* pNumWritten) {
	static const char* chars1 =
		"00000000001111111111222222222233333333334444444444"
		"55555555556666666666777777777788888888889999999999";
	static const char* chars2 =
		"01234567890123456789012345678901234567890123456789"
		"01234567890123456789012345678901234567890123456789";

	if (space < 1) {
		return OPA_ERR_INVARG;
	}

	char* pos = str;
	char* stop = str + space;

	if (opabigintIsNeg(a) && pos < stop) {
		*pos++ = '-';
	}
	char* digStart = pos;

	if (opabigintCountBits(a) > 64) {
		opabigint t;
		opabigintDigit r;
		opabigintInit(&t);
		int err = opabigintAbs(&t, a);
		if (!err) {
			while (pos < stop) {
				err = opabigintDivDig(&t, &r, &t, 100);
				if (err) {
					break;
				}
				if (opabigintIsZero(&t)) {
					if (r >= 10) {
						*pos++ = chars2[r];
						if (pos < stop) {
							*pos++ = chars1[r];
						}
					} else {
						*pos++ = '0' + r;
					}
					break;
				}
				*pos++ = chars2[r];
				if (pos < stop) {
					*pos++ = chars1[r];
				}
			}
		}
		opabigintFree(&t);
		if (err) {
			return err;
		}
	} else {
		uint64_t v = opabigintGetMagU64(a);

		while (pos < stop) {
			int r = v % 100;
			v = v / 100;
			if (v == 0) {
				if (r >= 10) {
					*pos++ = chars2[r];
					if (pos < stop) {
						*pos++ = chars1[r];
					}
				} else {
					*pos++ = '0' + r;
				}
				break;
			}
			*pos++ = chars2[r];
			if (pos < stop) {
				*pos++ = chars1[r];
			}
		}
	}

	if (pos >= stop) {
		return OPA_ERR_INVARG;
	}

	revdigs(digStart, pos - digStart);

	*pos++ = 0;

	if (pNumWritten != NULL) {
		*pNumWritten = pos - str;
	}

	return 0;
}

int opabigintToRadix(const opabigint* a, char* str, size_t space, size_t* pNumWritten, int radix) {
	if (space < 1 || radix < 2 || radix > 64) {
		return OPA_ERR_INVARG;
	}

	if (radix == 10) {
		return opabigintToRadix10(a, str, space, pNumWritten);
	}
	const char* const radixChars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";

	char* pos = str;
	char* stop = str + space;

	if (opabigintIsNeg(a) && pos < stop) {
		*pos++ = '-';
	}
	char* digStart = pos;

	if (opabigintCountBits(a) > 64) {
		opabigint t1;
		opabigint t2;
		opabigintDigit r;
		opabigintInit(&t1);
		opabigintInit(&t2);
		int err = opabigintAbs(&t1, a);
		while (!err && pos < stop) {
			err = opabigintDivDig(&t2, &r, &t1, radix);
			if (!err) {
				*pos++ = radixChars[r];
				if (opabigintIsZero(&t2)) {
					break;
				}
				err = opabigintCopy(&t1, &t2);
			}
		}
		opabigintFree(&t1);
		opabigintFree(&t2);
		if (err) {
			return err;
		}
	} else {
		uint64_t v = opabigintGetMagU64(a);

		while (pos < stop) {
			int r = v % radix;
			v = v / radix;
			*pos++ = radixChars[r];
			if (v == 0) {
				break;
			}
		}
	}

	if (pos >= stop) {
		return OPA_ERR_INVARG;
	}

	revdigs(digStart, pos - digStart);

	*pos++ = 0;

	if (pNumWritten != NULL) {
		*pNumWritten = pos - str;
	}

	return 0;
}

size_t opabigintWriteBytes(const opabigint* val, int useBigEndian, uint8_t* buff, size_t buffLen) {
	size_t bits = opabigintCountBits(val);
	if (bits == 0) {
		return 0;
	}
	size_t numBytes = (bits + 7) / 8;

	if (buffLen < numBytes) {
		return numBytes;
	}

	uint8_t* pos = useBigEndian ? buff + numBytes - 1 : buff;
	size_t digIdx = 0;
	opabigintDigit currDig = opabigintGetLimb(val, 0);
	size_t bitIdx = 0;

	while (bits > 0) {
		uint8_t byte = 0;

		if (bits >= 8 && bitIdx <= OPABIGINT_DIGIT_BITS - 8) {
			// read 8 bit chunk if possible
			byte = (currDig >> bitIdx) & 0xFF;
			bits -= 8;
			bitIdx += 8;
		} else {
			// read 8 bits, one at a time
			size_t numToLoad = bits >= 8 ? 8 : bits;
			for (size_t j = 0; j < numToLoad; ++j, --bits, ++bitIdx) {
				if (bitIdx == OPABIGINT_DIGIT_BITS) {
					currDig = opabigintGetLimb(val, ++digIdx);
					bitIdx = 0;
				}
				byte |= ((currDig >> bitIdx) & 0x1) << j;
			}
		}

		*pos = byte;
		if (useBigEndian) {
			--pos;
		} else {
			++pos;
		}
	}

	return numBytes;
}
