/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <limits.h>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "opacore.h"
#include "opapp.h"



#define UTF8FIRST 0
#define UTF8NEED1 1
#define UTF8NEED2 2
#define UTF8NEED3 3
#define UTF8R1    4
#define UTF8R2    5
#define UTF8R3    6
#define UTF8R4    7
#define UTF8ERR   8

#define UTF8CHECKRANGE(buff, buffEnd, start, end, rangeid, next) { \
	if (*buff >= start && *buff <= end){++buff; goto next;} \
	return (buff >= buffEnd) ? rangeid : UTF8ERR; \
}

#define UTF8CHECKBYTE(buff, buffEnd, rangeid, next) { \
	if ((*buff & 0xC0) == 0x80){++buff; goto next;} \
	return (buff >= buffEnd) ? rangeid : UTF8ERR; \
}

// note: buff must be null terminated
static uint8_t utf8check(const uint8_t* buff, size_t len, uint8_t state) {
	const uint8_t* end = buff + len;

//#ifdef __SSE2__
//	const uint8_t* sseEnd = buff + len - 16;
//#endif

	switch (state) {
		case UTF8FIRST: goto CHECKFIRST;
		case UTF8NEED1: goto CHECKNEED1;
		case UTF8NEED2: goto CHECKNEED2;
		case UTF8NEED3: goto CHECKNEED3;
		case UTF8R1:    goto CHECKR1;
		case UTF8R2:    goto CHECKR2;
		case UTF8R3:    goto CHECKR3;
		case UTF8R4:    goto CHECKR4;
		case UTF8ERR:
			return UTF8ERR;
		default:
			OPAPANIC("invalid state");
			//return UTF8ERR;
	}

	// UTF-8 rules: http://tools.ietf.org/html/rfc3629
	// 00-7F
	// C2-DF 80-BF
	// E0-E0 A0-BF 80-BF
	// E1-EC 80-BF 80-BF
	// ED-ED 80-9F 80-BF
	// EE-EF 80-BF 80-BF
	// F0-F0 90-BF 80-BF 80-BF
	// F1-F3 80-BF 80-BF 80-BF
	// F4-F4 80-8F 80-BF 80-BF

	CHECKFIRST:

//#ifdef __SSE2__
//	while (buff < sseEnd) {
//		// skip in chunks of 16 bytes if MSB is not set (chars are ASCII)
//		int mask = _mm_movemask_epi8(_mm_loadu_si128((const __m128i *) buff));
//		if (mask != 0) {
//			// this chunk contains a non-ascii character
//			buff += __builtin_ctz(mask);
//			break;
//		}
//		buff += 16;
//	}
//#endif

	while (buff < end) {
		if (*buff <= 0x7F) {
			++buff;
		} else if (*buff >= 0xC2 && *buff <= 0xDF) {
			++buff;
			goto CHECKNEED1;
		} else if (*buff == 0xE0) {
			++buff;
			goto CHECKR1;
		} else if (*buff >= 0xE1 && *buff <= 0xEC) {
			++buff;
			goto CHECKNEED2;
		} else if (*buff == 0xED) {
			++buff;
			goto CHECKR2;
		} else if (*buff >= 0xEE && *buff <= 0xEF) {
			++buff;
			goto CHECKNEED2;
		} else if (*buff == 0xF0) {
			++buff;
			goto CHECKR3;
		} else if (*buff >= 0xF1 && *buff <= 0xF3) {
			++buff;
			goto CHECKNEED3;
		} else if (*buff == 0xF4) {
			++buff;
			goto CHECKR4;
		} else {
			return UTF8ERR;
		}
	}
	return UTF8FIRST;

	CHECKR1:
	UTF8CHECKRANGE(buff, end, 0xA0, 0xBF, UTF8R1, CHECKNEED1);

	CHECKR2:
	UTF8CHECKRANGE(buff, end, 0x80, 0x9F, UTF8R2, CHECKNEED1);

	CHECKR3:
	UTF8CHECKRANGE(buff, end, 0x90, 0xBF, UTF8R3, CHECKNEED2);

	CHECKR4:
	UTF8CHECKRANGE(buff, end, 0x80, 0x8F, UTF8R4, CHECKNEED2);

	CHECKNEED3:
	UTF8CHECKBYTE(buff, end, UTF8NEED3, CHECKNEED2);

	CHECKNEED2:
	UTF8CHECKBYTE(buff, end, UTF8NEED2, CHECKNEED1);

	CHECKNEED1:
	UTF8CHECKBYTE(buff, end, UTF8NEED1, CHECKFIRST);

}


#define OPAPP_S_NEXTOBJ      0
#define OPAPP_S_VARINT2      1
#define OPAPP_S_VARDEC       2
#define OPAPP_S_BIGDEC       3
#define OPAPP_S_UTF8         4
#define OPAPP_S_SKIPBYTES    5
#define OPAPP_S_CHECKBIBYTES 6
#define OPAPP_S_RETURNOBJ    7
#define OPAPP_S_ERR          8


static int opappFindEndInternal(opapp* rc, const uint8_t* buff, size_t len, const uint8_t** pEnd, const opappOptions* opt) {
	OASSERT(buff[len] == 0);

	const uint8_t* end = buff + len;

	StateSwitch: {
		switch (rc->state) {
			case OPAPP_S_NEXTOBJ:      goto ParseNextObj;
			case OPAPP_S_VARINT2:      goto ParseVarint2;
			case OPAPP_S_VARDEC:       goto ParseVarDec;
			case OPAPP_S_BIGDEC:       goto ParseBigDec;
			case OPAPP_S_UTF8:         goto Utf8;
			case OPAPP_S_SKIPBYTES:    goto SkipBytes;
			case OPAPP_S_RETURNOBJ:    goto ReturnOrParseNextObj;
			case OPAPP_S_CHECKBIBYTES: goto CheckBigIntBytes;

			case OPAPP_S_ERR:          return OPA_ERR_PARSE;
			default:                   OPAPANIC("invalid state");
		}
	}

	ReturnOrParseNextObj: {
		if (rc->arrayDepth == 0) {
			rc->state = OPAPP_S_NEXTOBJ;
			*pEnd = buff;
			return 0;
		}
		goto ParseNextObj;
	}

	ParseNextObj: {
		switch (*buff++) {
			default:
			case 0:
				if (buff - 1 == end) {
					rc->state = OPAPP_S_NEXTOBJ;
					goto ReturnOK;
				}
				goto ReturnParseErr;

			case OPADEF_UNDEFINED:
			case OPADEF_NULL:
			case OPADEF_FALSE:
			case OPADEF_TRUE:
			case OPADEF_NEGINF:
			case OPADEF_POSINF:
			case OPADEF_ZERO:
			case OPADEF_BIN_EMPTY:
			case OPADEF_STR_EMPTY:
			case OPADEF_ARRAY_EMPTY:
			case OPADEF_SORTMAX:
				goto ReturnOrParseNextObj;

			case OPADEF_POSVARINT:
			case OPADEF_NEGVARINT:
				rc->varintNextState = OPAPP_S_RETURNOBJ;
				goto ParseVarInt1;

			case OPADEF_POSBIGINT:
			case OPADEF_NEGBIGINT:
				rc->varintNextState = OPAPP_S_CHECKBIBYTES;
				goto ParseVarInt1;

			case OPADEF_POSPOSVARDEC:
			case OPADEF_POSNEGVARDEC:
			case OPADEF_NEGPOSVARDEC:
			case OPADEF_NEGNEGVARDEC:
				rc->varintNextState = OPAPP_S_VARDEC;
				goto ParseVarInt1;

			case OPADEF_POSPOSBIGDEC:
			case OPADEF_POSNEGBIGDEC:
			case OPADEF_NEGPOSBIGDEC:
			case OPADEF_NEGNEGBIGDEC:
				rc->varintNextState = OPAPP_S_BIGDEC;
				goto ParseVarInt1;

			case OPADEF_BIN_LPVI:
				rc->varintNextState = OPAPP_S_SKIPBYTES;
				goto ParseVarInt1;

			case OPADEF_STR_LPVI:
				OASSERT(rc->utf8State == UTF8FIRST);
				rc->varintNextState = opt->checkUtf8 ? OPAPP_S_UTF8 : OPAPP_S_SKIPBYTES;
				goto ParseVarInt1;

			case OPADEF_ARRAY_START:
				if (rc->arrayDepth >= opt->maxArrayDepth) {
					goto ReturnParseErr;
				}
				++rc->arrayDepth;
				goto ParseNextObj;

			case OPADEF_ARRAY_END:
				if (rc->arrayDepth == 0) {
					goto ReturnParseErr;
				}
				if (--rc->arrayDepth == 0) {
					rc->state = OPAPP_S_NEXTOBJ;
					*pEnd = buff;
					return 0;
				}
				goto ParseNextObj;
		}
	}

	ParseVarInt1: {
		rc->varintLen = 0;
		rc->varintVal = 0;
	}

	ParseVarint2: {
		while ((buff[0] & 0x80) != 0 && rc->varintLen < 9) {
			rc->varintVal |= (((uint64_t)(*buff & 0x7F)) << (rc->varintLen * 7));
			++buff;
			++rc->varintLen;
		}
		if (buff == end) {
			rc->state = OPAPP_S_VARINT2;
			goto ReturnOK;
		}
		if (rc->varintLen >= 9 || (*buff == 0 && rc->varintLen > 0)) {
			// varint must be encoded with 1-9 bytes
			// varint cannot have 0 prefix (last byte cannot be zero for multi-byte varint)
			goto ReturnParseErr;
		}
		rc->varintVal |= (((uint64_t)(*buff & 0x7F)) << (rc->varintLen * 7));
		++buff;
		rc->state = rc->varintNextState;
		goto StateSwitch;
	}

	Utf8: {
		//rc->utf8State = utf8check(buff, MIN(rc->varintVal, (size_t) (end - buff)), rc->utf8State);
		rc->utf8State = utf8check(buff, rc->varintVal < (size_t) (end - buff) ? rc->varintVal : (size_t) (end - buff), rc->utf8State);
		if (rc->utf8State == UTF8ERR) {
			// invalid utf-8 bytes
			goto ReturnParseErr;
		}
		if (buff + rc->varintVal > end) {
			OASSERT(buff <= end);
			rc->varintVal -= end - buff;
			OASSERT(rc->state == OPAPP_S_UTF8);
			goto ReturnOK;
		}
		if (rc->utf8State != UTF8FIRST) {
			goto ReturnParseErr;
		}
		buff += rc->varintVal;
		goto ReturnOrParseNextObj;
	}

	CheckBigIntBytes: {
		if (rc->varintVal == 0 || rc->varintVal > opt->maxBigIntLen) {
			// bigint or bigdec significand has a byte-len of 0 (byte-len must be >0) or is too long
			goto ReturnParseErr;
		}
		if (buff + 1 > end) {
			OASSERT(rc->state == OPAPP_S_CHECKBIBYTES);
			goto ReturnOK;
		}
		if (*buff == 0 && rc->varintVal > 1) {
			// MSB cannot be 0 if byte-len is >1
			goto ReturnParseErr;
		}
		goto SkipBytes;
	}

	SkipBytes: {
		if (buff + rc->varintVal > end) {
			OASSERT(buff <= end);
			rc->varintVal -= end - buff;
			rc->state = OPAPP_S_SKIPBYTES;
			goto ReturnOK;
		}
		buff += rc->varintVal;
		goto ReturnOrParseNextObj;
	}

	ParseVarDec: {
		if (rc->varintVal > opt->maxDecExp) {
			// exponent is too big
			goto ReturnParseErr;
		}
		rc->varintNextState = OPAPP_S_RETURNOBJ;
		goto ParseVarInt1;
	}

	ParseBigDec: {
		if (rc->varintVal > opt->maxDecExp) {
			// exponent is too big
			goto ReturnParseErr;
		}
		rc->varintNextState = OPAPP_S_CHECKBIBYTES;
		goto ParseVarInt1;
	}

	ReturnParseErr: {
		rc->state = OPAPP_S_ERR;
		return OPA_ERR_PARSE;
	}

	ReturnOK: {
		*pEnd = NULL;
		return 0;
	}
}

static const opappOptions OPAPP_DEFOPT = {
	.maxArrayDepth = UINT_MAX,
	.checkUtf8 = 1,
	.maxBigIntLen = SIZE_MAX,
	.maxDecExp = INT32_MAX,
	//.maxBinLen = SIZE_MAX,
	//.maxStrLen = SIZE_MAX
};

int opappFindEnd(opapp* rc, const uint8_t* buff, size_t len, const uint8_t** pEnd, const opappOptions* opt) {
	return opappFindEndInternal(rc, buff, len, pEnd, opt == NULL ? &OPAPP_DEFOPT : opt);
}
