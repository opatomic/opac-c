/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <string.h>

#include "base64.h"
#include "opabigdec.h"
#include "opacore.h"
#include "oparb.h"
#include "opaso.h"


static void oparbAppend1(oparb* rb, uint8_t b) {
	if (!rb->err) {
		rb->err = opabuffAppend1(&rb->buff, b);
	}
}

static void oparbAppend(oparb* rb, const void* src, size_t srcLen) {
	if (!rb->err) {
		rb->err = opabuffAppend(&rb->buff, src, srcLen);
	}
}

void oparbInit(oparb* rb, const uint8_t* asyncId, size_t idLen) {
	memset(rb, 0, sizeof(oparb));
	oparbAppend1(rb, OPADEF_ARRAY_START);
	oparbAppend(rb, asyncId, idLen);
}

static void oparbAppendStrOrBin(oparb* rb, size_t len, const void* arg, uint8_t type) {
	if (!rb->err) {
		if (len == 0) {
			rb->err = opabuffAppend1(&rb->buff, type == OPADEF_STR_LPVI ? OPADEF_STR_EMPTY : OPADEF_BIN_EMPTY);
		} else {
			size_t pos = opabuffGetLen(&rb->buff);
			rb->err = opabuffAppend(&rb->buff, NULL, 1 + opaviStoreLen(len) + len);
			if (!rb->err) {
				uint8_t* buff = opabuffGetPos(&rb->buff, pos);
				*buff = type;
				buff = opaviStore(len, buff + 1);
				if (arg != NULL) {
					memcpy(buff, arg, len);
				}
			}
		}
	}
}

static void oparbWriteVarint(oparb* rb, uint8_t type, uint64_t val) {
	if (!rb->err) {
		size_t pos = opabuffGetLen(&rb->buff);
		rb->err = opabuffAppend(&rb->buff, NULL, 1 + opaviStoreLen(val));
		if (!rb->err) {
			uint8_t* buff = opabuffGetPos(&rb->buff, pos);
			*buff = type;
			opaviStore(val, buff + 1);
		}
	}
}

void oparbFinish(oparb* rb) {
	if (!rb->err) {
		int empty = 1;
		size_t blen = opabuffGetLen(&rb->buff);
		if (blen > 1) {
			const uint8_t* pos = opabuffGetPos(&rb->buff, 0);
			if (*pos == OPADEF_ARRAY_START) {
				size_t idlen = opasolen(pos + 1);
				if (blen > 1 + idlen) {
					empty = 0;
				}
			}
		}
		if (empty) {
			rb->err = OPA_ERR_INVSTATE;
			rb->errDesc = "empty request";
		}
	}
	if (rb->depth > 0 && !rb->err) {
		rb->err = OPA_ERR_INVSTATE;
		rb->errDesc = "invalid array depth";
	}
	oparbAppend1(rb, OPADEF_ARRAY_END);
	if (rb->err) {
		opabuffFree(&rb->buff);
	}
}

static void oparbAddBigDec(oparb* rb, const opabigdec* arg) {
	if (!rb->err) {
		size_t pos = opabuffGetLen(&rb->buff);
		size_t lenReq = opabigdecStoreSO(arg, NULL, 0);
		rb->err = opabuffAppend(&rb->buff, NULL, lenReq);
		if (!rb->err) {
			opabigdecStoreSO(arg, opabuffGetPos(&rb->buff, pos), lenReq);
		}
	}
}

static void oparbAddVarint(oparb* rb, uint8_t type, uint64_t val) {
	if (val == 0) {
		oparbAppend1(rb, OPADEF_ZERO);
	} else if (val <= INT64_MAX) {
		oparbWriteVarint(rb, type, val);
	} else {
		// val too big for varint
		if (!rb->err) {
			opabigdec bd;
			rb->err = opabigdecInit(&bd);
			if (!rb->err) {
				rb->err = opabigdecSet64(&bd, val, type == OPADEF_NEGVARINT, 0);
				if (!rb->err) {
					oparbAddBigDec(rb, &bd);
				}
				opabigdecClear(&bd);
			}
		}
	}
}

void oparbAddI64(oparb* rb, int64_t arg) {
	if (arg < 0) {
		// TODO: test INT64_MIN and INT64_MIN - 1
		oparbAddVarint(rb, OPADEF_NEGVARINT, 0 - arg);
	} else {
		oparbAddVarint(rb, OPADEF_POSVARINT, arg);
	}
}

void oparbAddU64(oparb* rb, uint64_t arg) {
	oparbAddVarint(rb, OPADEF_POSVARINT, arg);
}

void oparbAddSO(oparb* rb, const uint8_t* so) {
	oparbAppend(rb, so, opasolen(so));
}

void oparbAddNumStr(oparb* rb, const char* s) {
	if (!rb->err) {
		opabigdec bd;
		rb->err = opabigdecInit(&bd);
		if (!rb->err) {
			rb->err = opabigdecFromStr(&bd, s, 10);
			if (!rb->err) {
				oparbAddBigDec(rb, &bd);
			}
			opabigdecClear(&bd);
		}
	}
}

void oparbAddBin(oparb* rb, size_t len, const void* arg) {
	oparbAppendStrOrBin(rb, len, arg, OPADEF_BIN_LPVI);
}

void oparbAddStr(oparb* rb, size_t len, const void* arg) {
	// TODO: check whether chars are valid UTF-8?
	oparbAppendStrOrBin(rb, len, arg, OPADEF_STR_LPVI);
}

void oparbStartArray(oparb* rb) {
	oparbAppend1(rb, OPADEF_ARRAY_START);
	if (!rb->err) {
		++rb->depth;
	}
}

void oparbStopArray(oparb* rb) {
	if (rb->depth == 0) {
		if (!rb->err) {
			rb->err = OPA_ERR_INVSTATE;
			rb->errDesc = "invalid array depth";
		}
		return;
	}
	if (!rb->err) {
		OASSERT(opabuffGetLen(&rb->buff) > 0);
		uint8_t* prevByte = opabuffGetPos(&rb->buff, opabuffGetLen(&rb->buff) - 1);
		if (prevByte != NULL && *prevByte == OPADEF_ARRAY_START) {
			*prevByte = OPADEF_ARRAY_EMPTY;
		} else {
			oparbAppend1(rb, OPADEF_ARRAY_END);
		}
	}
	if (!rb->err) {
		--rb->depth;
	}
}









static int startsWith(const char* s, const char* prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static uint32_t hexVal(char ch) {
	if (ch <= '9' && ch >= '0') {
		return ch - '0';
	} else if (ch <= 'F' && ch >= 'A') {
		return ch - 'A' + 10;
	} else if (ch <= 'f' && ch >= 'a') {
		return ch - 'a' + 10;
	}
	return 0xFFFFFFFF;
}

// TODO: use isalnum instead?
static int isalphanum(int ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

static int isValidEscapeChar(int ch) {
	if (isalphanum(ch)) {
		switch (ch) {
			case 'b':
			case 'f':
			case 'n':
			case 'r':
			case 't':
			case 'u':
			case 'x':
				return 1;
			default:
				return 0;
		}
	} else if (ch <= 0x20) {
		return ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
	} else {
		return ch != 0x7f;
	}
}

static int oparbStrUnescape(const char* s, const char* end, opabuff* b) {
	int err = 0;
	for (; !err && s < end; ++s) {
		char ch = *s;
		if (ch == '\\') {
			++s;
			if (s >= end || !isValidEscapeChar(*s)) {
				return OPA_ERR_PARSE;
			}
			switch (*s) {
				case 'b':  err = opabuffAppend1(b, '\b'); break;
				case 'f':  err = opabuffAppend1(b, '\f'); break;
				case 'n':  err = opabuffAppend1(b, '\n'); break;
				case 'r':  err = opabuffAppend1(b, '\r'); break;
				case 't':  err = opabuffAppend1(b, '\t'); break;
				case 'x': {
					if (s + 3 > end) {
						return OPA_ERR_PARSE;
					}
					uint32_t uchar = (hexVal(s[1]) << 4) | hexVal(s[2]);
					if (uchar > 0xFF) {
						return OPA_ERR_PARSE;
					}
					err = opabuffAppend1(b, uchar);
					s += 2;
					break;
				}
				case 'u': {
					if (s + 5 > end) {
						return OPA_ERR_PARSE;
					}
					uint32_t uchar = (hexVal(s[1]) << 12) | (hexVal(s[2]) << 8) | (hexVal(s[3]) << 4) | hexVal(s[4]);
					if (uchar > 0xFFFF) {
						return OPA_ERR_PARSE;
					}

					if (uchar < 0x80) {
						err = opabuffAppend1(b, uchar & 0xFF);
					} else if (uchar < 0x0800) {
						err = opabuffAppend1(b, 0xC0 | ((uchar >> 6) & 0x1F));
						if (!err) {
							err = opabuffAppend1(b, 0x80 | (uchar & 0x3F));
						}
					} else if (uchar < 0xD800 || uchar > 0xDFFF) {
						err = opabuffAppend1(b, 0xE0 | ((uchar >> 12) & 0x0F));
						if (!err) {
							err = opabuffAppend1(b, 0x80 | ((uchar >> 6) & 0x3F));
						}
						if (!err) {
							err = opabuffAppend1(b, 0x80 | (uchar & 0x3F));
						}
					} else {
						// surrogate pair
						if (uchar >= 0xDC00) {
							// 0xDC00-0xDFFF is an invalid 1st value (must be 2nd value of a surrogate pair)
							return OPA_ERR_PARSE;
						}
						if (s + 11 > end || s[5] != '\\' || s[6] != 'u') {
							return OPA_ERR_PARSE;
						}
						uint32_t uchar2 = (hexVal(s[7]) << 12) | (hexVal(s[8]) << 8) | (hexVal(s[9]) << 4) | hexVal(s[10]);
						if (uchar2 > 0xFFFF) {
							return OPA_ERR_PARSE;
						}
						if (uchar2 < 0xDC00 || uchar2 > 0xDFFF) {
							// 2nd value of surrogate pair must be 0xDC00-0xDFFF
							return OPA_ERR_PARSE;
						}

						// convert to utf32
						// See RFC 2781, Section 2.2
						// http://www.ietf.org/rfc/rfc2781.txt
						int32_t code = (((uchar & 0x3FF) << 10) | (uchar2 & 0x3FF)) + 0x10000;
						// convert to utf8
						err = opabuffAppend1(b, 0xF0 | (code >> 18));
						if (!err) {
							err = opabuffAppend1(b, 0x80 | ((code >> 12) & 0x3F));
						}
						if (!err) {
							err = opabuffAppend1(b, 0x80 | ((code >> 6) & 0x3F));
						}
						if (!err) {
							err = opabuffAppend1(b, 0x80 | (code & 0x3F));
						}
						s += 6;
					}
					s += 4;
					break;
				}
				default:
					err = opabuffAppend1(b, *s);
					break;
			}
		} else {
			err = opabuffAppend1(b, ch);
		}
	}
	return err;
}

static void oparbAddUnEscapedStr(oparb* rb, const char* s, size_t len) {
	if (!rb->err && len > 0 && *s == '~') {
		if (len >= 4 && startsWith(s, "~bin")) {
			oparbAddBin(rb, len - 4, s + 4);
			return;
		} else if (len >= 7 && startsWith(s, "~base64")) {
			s += 7;
			len -= 7;
			size_t decLen = base64DecodeLen((const uint8_t*) s, len);
			oparbAddBin(rb, decLen, NULL);
			if (!rb->err && !base64Decode(s, len, opabuffGetPos(&rb->buff, opabuffGetLen(&rb->buff) - decLen))) {
				rb->errDesc = "base64 is invalid";
				rb->err = OPA_ERR_PARSE;
			}
			return;
		} else if (len >= 2 && (s[1] == '~' || s[1] == '^' || s[1] == '`')) {
			++s;
			--len;
		} else {
			rb->errDesc = "unknown ~ prefix sequence";
			rb->err = OPA_ERR_PARSE;
			return;
		}
	}
	if (!rb->err) {
		const uint8_t* invutf8 = opaFindInvalidUtf8((const uint8_t*)s, len);
		if (invutf8 == NULL) {
			oparbAddStr(rb, len, s);
		} else {
			oparbAddBin(rb, len, s);
		}
	}
}

static void oparbAddUserString(oparb* rb, const char* s, const char* end) {
	opabuff tmp = {0};
	if (!rb->err) {
		rb->err = oparbStrUnescape(s, end, &tmp);
		if (rb->err == OPA_ERR_PARSE) {
			rb->errDesc = "invalid escape sequence";
		}
	}
	if (!rb->err) {
		oparbAddUnEscapedStr(rb, (const char*) opabuffGetPos(&tmp, 0), opabuffGetLen(&tmp));
	}
	opabuffFree(&tmp);
}

static int oparbIsToken(const void* s, size_t slen, const char* t) {
	return slen == strlen(t) && memcmp(s, t, slen) == 0;
}

static uint8_t oparbConvertToken(const void* s, size_t slen) {
	if (oparbIsToken(s, slen, "undefined")) {
		return OPADEF_UNDEFINED;
	} else if (oparbIsToken(s, slen, "null")) {
		return OPADEF_NULL;
	} else if (oparbIsToken(s, slen, "false")) {
		return OPADEF_FALSE;
	} else if (oparbIsToken(s, slen, "true")) {
		return OPADEF_TRUE;
	} else if (oparbIsToken(s, slen, "SORTMAX")) {
		return OPADEF_SORTMAX;
	}
	return 0;
}

static void oparbAddUserToken(oparb* rb, const char* s, const char* end) {
	uint8_t replacement = oparbConvertToken(s, end - s);
	if (replacement != 0) {
		oparbAppend1(rb, replacement);
	} else if (opaIsNumStr(s, end)) {
		oparbAddNumStr(rb, s);
	} else {
		oparbAddUserString(rb, s, end);
	}
}

static const char* oparbFindQuoteEnd(const char* str) {
	while (1) {
		if (*str == '"') {
			return str;
		} else if (*str == '\\') {
			++str;
			if (*str == 0) {
				return NULL;
			}
		} else if (*str == 0) {
			return NULL;
		}
		++str;
	}
}

static const char* oparbFindTokenEnd(const char* str) {
	while (1) {
		int ch = *str;
		// TODO: whitelist some more characters that can be unquoted and unescaped? ie ":/*?"
		// note: slash character '/' cannot be included here because it is used for comments
		// note: / is included to help with key separators and is also used by base64
		if (isalphanum(ch) || ch < 0 || ch == '_' || ch == '.' || ch == '-' || ch == '+') {
			++str;
		} else if (ch == '\\' && str[1] != 0) {
			str += 2;
		} else {
			return str;
		}
	}
}

static oparb oparbParseUserCommandWithId(const char* s, const uint8_t* id, size_t idLen) {
	oparb rb;
	oparbInit(&rb, id, idLen);
	int depth = 0;
	const char* end;
	while (!rb.err) {
		switch (*s) {
			case 0:
				goto Done;
			case '"':
				++s;
				end = oparbFindQuoteEnd(s);
				if (end == NULL) {
					rb.errDesc = "string end quote not found";
					rb.err = OPA_ERR_PARSE;
					goto Done;
				}
				oparbAddUserString(&rb, s, end);
				s = end + 1;
				break;
			case '[':
				oparbStartArray(&rb);
				++depth;
				++s;
				break;
			case ']':
				if (depth <= 0) {
					rb.errDesc = "extra array end token ']'";
					rb.err = OPA_ERR_PARSE;
					goto Done;
				}
				oparbStopArray(&rb);
				--depth;
				++s;
				break;
			case ',':
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				++s;
				break;
			default:
				end = oparbFindTokenEnd(s);
				if (end == s) {
					rb.errDesc = "reserved/special/control characters must be inside quotes or escaped";
					rb.err = OPA_ERR_PARSE;
					goto Done;
				}
				oparbAddUserToken(&rb, s, end);
				s = end;
				break;
		}
	}
	Done:
	if (!rb.err && depth > 0) {
		rb.errDesc = "array end token ']' not found";
		rb.err = OPA_ERR_PARSE;
	}
	oparbFinish(&rb);
	return rb;
}

oparb oparbParseUserCommand(const char* s) {
	const uint8_t idbuff[] = {OPADEF_NULL};
	return oparbParseUserCommandWithId(s, idbuff, 1);
}
