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

void oparbInit(oparb* rb) {
	memset(rb, 0, sizeof(oparb));
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

void oparbSetCommand(oparb* rb, const char* cmd) {
	if (rb->started) {
		if (!rb->err) {
			rb->err = OPA_ERR_INVSTATE;
			rb->errDesc = "command already set";
		}
		return;
	}
	oparbAddStr(rb, strlen(cmd), cmd);
}

static void oparbStartIfNeeded(oparb* rb) {
	if (!rb->started) {
		oparbAppend1(rb, OPADEF_ARRAY_START);
		if (!rb->err) {
			rb->started = 1;
		}
	} else if (!rb->argsStarted && rb->depth == 0) {
		oparbAppend1(rb, OPADEF_ARRAY_START);
		if (!rb->err) {
			rb->argsStarted = 1;
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

static void oparbFinishAsyncSO(oparb* rb, const uint8_t* asyncIdSO) {
	if (!rb->started && !rb->err) {
		rb->err = OPA_ERR_INVSTATE;
		rb->errDesc = "empty request";
	}
	if (rb->depth > 0 && !rb->err) {
		rb->err = OPA_ERR_INVSTATE;
		rb->errDesc = "invalid array depth";
	}
	if (rb->argsStarted) {
		oparbAppend1(rb, OPADEF_ARRAY_END);
	}
	if (asyncIdSO != NULL) {
		if (!rb->argsStarted) {
			oparbAppend1(rb, OPADEF_NULL);
		}
		oparbAppend(rb, asyncIdSO, opasolen(asyncIdSO));
	}
	oparbAppend1(rb, OPADEF_ARRAY_END);
	if (rb->err) {
		opabuffFree(&rb->buff);
	}
}

void oparbFinish(oparb* rb) {
	oparbFinishAsyncSO(rb, NULL);
}

static void oparbAddBigDec(oparb* rb, const opabigdec* arg) {
	oparbStartIfNeeded(rb);
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
	oparbStartIfNeeded(rb);
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
	oparbStartIfNeeded(rb);
	oparbAppend(rb, so, opasolen(so));
}

static void oparbAddStrOrBin(oparb* rb, size_t len, const void* arg, uint8_t type) {
	oparbStartIfNeeded(rb);
	oparbAppendStrOrBin(rb, len, arg, type);
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
	oparbAddStrOrBin(rb, len, arg, OPADEF_BIN_LPVI);
}

void oparbAddStr(oparb* rb, size_t len, const void* arg) {
	// TODO: check whether chars are valid UTF-8?
	oparbAddStrOrBin(rb, len, arg, OPADEF_STR_LPVI);
}

void oparbStartArray(oparb* rb) {
	oparbStartIfNeeded(rb);
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
	oparbAppend1(rb, OPADEF_ARRAY_END);
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

static int oparbStrUnescape(const char* s, const char* end, opabuff* b) {
	int err = 0;
	for (; !err && s < end; ++s) {
		char ch = *s;
		if (ch == '\\') {
			++s;
			switch (*s) {
				case '"':  err = opabuffAppend1(b, '"' ); break;
				case '\\': err = opabuffAppend1(b, '\\'); break;
				case '/':  err = opabuffAppend1(b, '/' ); break;
				case 'b':  err = opabuffAppend1(b, '\b'); break;
				case 'f':  err = opabuffAppend1(b, '\f'); break;
				case 'n':  err = opabuffAppend1(b, '\n'); break;
				case 'r':  err = opabuffAppend1(b, '\r'); break;
				case 't':  err = opabuffAppend1(b, '\t'); break;
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
					} else {
						err = opabuffAppend1(b, 0xE0 | ((uchar >> 12) & 0x0F));
						if (!err) {
							err = opabuffAppend1(b, 0x80 | ((uchar >> 6) & 0x3F));
						}
						if (!err) {
							err = opabuffAppend1(b, 0x80 | (uchar & 0x3F));
						}
					}
					s += 4;
					break;
				}
				default:
					return OPA_ERR_PARSE;
			}
		} else {
			err = opabuffAppend1(b, ch);
		}
	}
	return err;
}

static void oparbAddUserString(oparb* rb, const char* s, const char* end) {
	if (end - s > 0 && *s == '~') {
		if (end - s >= 4 && startsWith(s, "~bin")) {
			s += 4;
			oparbAddBin(rb, end - s, s);
			return;
		} else if (end - s >= 7 && startsWith(s, "~base64")) {
			s += 7;
			size_t encLen = end - s;
			size_t decLen = base64DecodeLen((const uint8_t*) s, encLen);
			oparbAddStrOrBin(rb, decLen, NULL, OPADEF_BIN_LPVI);
			if (!rb->err && !base64Decode(s, encLen, opabuffGetPos(&rb->buff, opabuffGetLen(&rb->buff) - decLen))) {
				rb->errDesc = "base64 is invalid";
				rb->err = OPA_ERR_PARSE;
			}
			return;
		} else if (end - s >= 2 && (s[1] == '~' || s[1] == '^' || s[1] == '`')) {
			++s;
		}
	}
	opabuff tmp = {0};
	if (!rb->err) {
		rb->err = oparbStrUnescape(s, end, &tmp);
		if (rb->err == OPA_ERR_PARSE) {
			rb->errDesc = "invalid escape sequence";
		}
	}
	oparbAddStr(rb, opabuffGetLen(&tmp), opabuffGetPos(&tmp, 0));
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
		oparbStartIfNeeded(rb);
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
		switch (*str) {
			case '\\':
				if (str[1] != 0) {
					str++;
				}
				break;
			case 0:
			case ',':
			case '[':
			case ']':
			case '{':
			case '}':
			case '"':
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				return str;
		}
		++str;
	}
}

oparb oparbParseUserCommand(const char* s) {
	oparb rb;
	oparbInit(&rb);
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
			case '{':
			case '}':
				rb.errDesc = "object tokens '{' and '}' not supported";
				rb.err = OPA_ERR_PARSE;
				goto Done;
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
