/*
 * Copyright Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <string.h>

#include "opabigdec.h"
#include "opabuff.h"
#include "opacore.h"
#include "opaso.h"

static const char* HEXCHARS = "0123456789ABCDEF";

static int opabuffAppendStr(opabuff* b, const char* str) {
	return opabuffAppend(b, str, strlen(str));
}

static int opasoWriteIndent(opabuff* b, const char* space, unsigned int depth) {
	int err = 0;
	if (space != NULL) {
		size_t slen = strlen(space);
		if (slen > 0) {
			err = opabuffAppend1(b, '\n');
			for (; depth > 0 && !err; --depth) {
				err = opabuffAppend(b, space, slen);
			}
		}
	}
	return err;
}

static int opasoEscapeString(const uint8_t* src, size_t len, int isBin, opabuff* b) {
	int err = 0;
	const uint8_t* end = src + len;
	for (; src < end && !err; ++src) {
		uint8_t ch = *src;
		switch (ch) {
			case '"':  err = opabuffAppendStr(b, isBin ? "\""  : "\\\""); break;
			case '\'': err = opabuffAppendStr(b, isBin ? "\\'" : "'"   ); break;
			case '\\': err = opabuffAppendStr(b, "\\\\"); break;
			case '\t': err = opabuffAppendStr(b, "\\t" ); break;
			case '\r': err = opabuffAppendStr(b, "\\r" ); break;
			case '\n': err = opabuffAppendStr(b, "\\n" ); break;
			case '\b': err = opabuffAppendStr(b, "\\b" ); break;
			case '\f': err = opabuffAppendStr(b, "\\f" ); break;
			default:
				if (ch < 0x20 || ch == 0x7f) {
					// escape control chars
					// note: according to json specs, 0x7f can remain unescaped, however it's a
					//       control character that may not be visible - so escape it here.
					// TODO: also escape \u0080-\u009f? other unicode control characters?
					//       https://stackoverflow.com/questions/3770117/what-is-the-range-of-unicode-printable-characters
					err = opabuffAppendStr(b, isBin ? "\\x" : "\\u00");
					if (!err) {
						uint8_t tmp[2];
						tmp[0] = HEXCHARS[(ch & 0xF0) >> 4];
						tmp[1] = HEXCHARS[(ch & 0x0F)];
						err = opabuffAppend(b, tmp, 2);
					}
				} else {
					err = opabuffAppend1(b, ch);
				}
				break;
		}
	}
	return err;
}

static int opasoEscapeBin(const uint8_t* src, size_t len, opabuff* b) {
	int err = 0;
	const uint8_t* end = src + len;
	while (src < end && !err) {
		const uint8_t* invch = opaFindInvalidUtf8(src, end - src);
		if (invch == NULL) {
			err = opasoEscapeString(src, end - src, 1, b);
			break;
		}
		err = opasoEscapeString(src, invch - src, 1, b);
		if (!err) {
			char tmp[4] = {'\\', 'x', HEXCHARS[((*invch) >> 4) & 0xF], HEXCHARS[(*invch) & 0xF]};
			err = opabuffAppend(b, tmp, 4);
		}
		src = invch + 1;
	}
	return err;
}

static int opasoStringifyInternal(const uint8_t* src, const char* space, unsigned int depth, opabuff* b) {
	// TODO: add option to output JSON (ie, escape undefined/sortmax as something like ~U ~SM)
	switch (*src) {
		case OPADEF_UNDEFINED:   return opabuffAppendStr(b, "undefined");
		case OPADEF_NULL:        return opabuffAppendStr(b, "null");
		case OPADEF_FALSE:       return opabuffAppendStr(b, "false");
		case OPADEF_TRUE:        return opabuffAppendStr(b, "true");
		case OPADEF_SORTMAX:     return opabuffAppendStr(b, "SORTMAX");
		case OPADEF_BIN_EMPTY:   return opabuffAppendStr(b, "''");
		case OPADEF_STR_EMPTY:   return opabuffAppendStr(b, "\"\"");
		case OPADEF_ARRAY_EMPTY: return opabuffAppendStr(b, "[]");

		case OPADEF_ARRAY_START: {
			++src;
			if (*src == OPADEF_ARRAY_END) {
				return opabuffAppendStr(b, "[]");
			}
			int err = opabuffAppend1(b, '[');
			if (!err) {
				err = opasoWriteIndent(b, space, depth + 1);
			}
			while (!err) {
				err = opasoStringifyInternal(src, space, depth + 1, b);
				if (!err) {
					// TODO: don't call opasolen() here; would need to return end pos from opasoStringifyInternal()
					src += opasolen(src);
					if (*src == OPADEF_ARRAY_END) {
						break;
					}
					err = opabuffAppend1(b, ',');
				}
				if (!err) {
					err = opasoWriteIndent(b, space, depth + 1);
				}
			}
			if (!err) {
				err = opasoWriteIndent(b, space, depth);
			}
			if (!err) {
				err = opabuffAppend1(b, ']');
			}
			return err;
		}
		case OPADEF_BIN_LPVI: {
			int err = opabuffAppend1(b, '\'');
			if (!err) {
				uint64_t slen;
				err = opaviLoadWithErr(src + 1, &slen, &src);
				if (!err) {
					err = opasoEscapeBin(src, slen, b);
				}
			}
			if (!err) {
				err = opabuffAppend1(b, '\'');
			}
			return err;
		}
		case OPADEF_STR_LPVI: {
			int err = opabuffAppend1(b, '"');
			if (!err) {
				uint64_t slen;
				err = opaviLoadWithErr(src + 1, &slen, &src);
				if (!err) {
					err = opasoEscapeString(src, slen, 0, b);
				}
			}
			if (!err) {
				err = opabuffAppend1(b, '"');
			}
			return err;
		}

		case OPADEF_ZERO:
			return opabuffAppend1(b, '0');
		case OPADEF_NEGINF:
		case OPADEF_POSINF:
		case OPADEF_NEGVARINT:
		case OPADEF_POSVARINT:
		case OPADEF_NEGBIGINT:
		case OPADEF_POSBIGINT:
		case OPADEF_POSPOSVARDEC:
		case OPADEF_POSNEGVARDEC:
		case OPADEF_NEGPOSVARDEC:
		case OPADEF_NEGNEGVARDEC:
		case OPADEF_POSPOSBIGDEC:
		case OPADEF_POSNEGBIGDEC:
		case OPADEF_NEGPOSBIGDEC:
		case OPADEF_NEGNEGBIGDEC: {
			opabigdec bd;
			opabigdecInit(&bd);
			int err = opabigdecLoadSO(&bd, src);
			if (!err) {
				size_t maxlen = opabigdecMaxStringLen(&bd, 10);
				size_t origLen = opabuffGetLen(b);
				err = opabuffSetLen(b, origLen + maxlen + 1);
				if (!err) {
					char* strpos = (char*) opabuffGetPos(b, origLen);
					size_t lenWithNull;
					err = opabigdecToString(&bd, strpos, maxlen, &lenWithNull, 10);
					if (!err) {
						OASSERT(lenWithNull > 0);
						err = opabuffSetLen(b, origLen + lenWithNull - 1);
					}
				}
			}
			opabigdecFree(&bd);
			return err;
		}

		default: return OPA_ERR_PARSE;
	}
}

// note: does not append null char (cannot use strlen() to get length)
int opasoStringifyToBuff(const uint8_t* src, const char* space, opabuff* b) {
	size_t origLen = opabuffGetLen(b);
	int err = opasoStringifyInternal(src, space, 0, b);
	if (err) {
		opabuffSetLen(b, origLen);
	}
	return err;
}

char* opasoStringify(const uint8_t* src, const char* space) {
	if (src == NULL) {
		return NULL;
	}
	opabuff b;
	opabuffInit(&b, 0);
	int err = opasoStringifyInternal(src, space, 0, &b);
	if (!err) {
		err = opabuffAppend1(&b, 0);
	}
	if (err) {
		opabuffFree(&b);
		return NULL;
	}
	return (char*) opabuffGetPos(&b, 0);
}
