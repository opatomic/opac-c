/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef _WIN32
	#include <windows.h>
	// note: windows does not have flockfile or funlockfile
	//   _lock_file and _unlock_file exist but may not be supported in older versions of windows?
	//#define flockfile _lock_file
	//#define funlockfile _unlock_file

	// define stderr to stdout because eclipse isn't logging stderr to console properly. not sure why
	// TODO: figure out how to fix this
	#if !defined(OPA_STDERR) && defined(OPADBG)
		#define OPA_STDERR stdout
	#endif
	#define OPA_DIRCHAR '\\'
#else
	#define _GNU_SOURCE // strerror_r
	#include <sys/time.h>
	#define OPA_DIRCHAR '/'
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opacore.h"


#define OPA_LOGSTRPRE "%s(%s:%d): "
#define OPA_ERRSTRPRE "error in " OPA_LOGSTRPRE

#define TMPBUFFLEN 512

#ifndef OPA_STDERR
	#define OPA_STDERR stderr
#endif


#ifdef _WIN32

#define LOGINTERNAL(f, fmt1, func, filename, line, fmt2) \
	char tmp[TMPBUFFLEN];                                \
	va_list args;                                        \
	va_start(args, fmt2);                                \
	vsnprintf(tmp, sizeof(tmp), fmt2, args);             \
	va_end(args);                                        \
	fprintf(f, fmt1 "%s\n", func, filename, line, tmp);

uint64_t opaTimeMillis(void) {
	// https://stackoverflow.com/questions/1695288/getting-the-current-time-in-milliseconds-from-the-system-clock-in-windows
	FILETIME t;
	GetSystemTimeAsFileTime(&t);
	return (((((uint64_t) t.dwHighDateTime) << 32LL) | ((uint64_t)t.dwLowDateTime)) / 10000LL) - 116444736000000000LL;
}

void opacoreLogWinErrCode(const char* func, const char* filename, int line, DWORD err) {
	// TODO: have FormatMessage() allocate buffer to reduce stack size required? error cases should be infrequent so performance would not be affected
	const char* msg = "cannot determine err string in FormatMessage()";
	char tmpbuff[TMPBUFFLEN];
	if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), tmpbuff, sizeof(tmpbuff), NULL) != 0) {
		msg = tmpbuff;
	}
	opacoreLogErrf(func, filename, line, "system error %lu; %s", err, msg);
}

#else

#define LOGINTERNAL(f, fmt1, func, filename, line, fmt2) \
	va_list args;                                        \
	va_start(args, fmt2);                                \
	flockfile(f);                                        \
	fprintf(f, fmt1, func, filename, line);              \
	vfprintf(f, fmt2, args);                             \
	fprintf(f, "\n");                                    \
	funlockfile(f);                                      \
	va_end(args);

uint64_t opaTimeMillis(void) {
	struct timeval t;
	if (gettimeofday(&t, NULL)) {
		LOGSYSERRNO();
		return 0;
	}
	return (t.tv_sec * 1000) + (t.tv_usec / 1000);
}

#endif


const char* opacoreFileBasename(const char* file) {
	const char* pos = strrchr(file, OPA_DIRCHAR);
	return pos == NULL ? file : pos + 1;
}

void opacoreLogf(const char* func, const char* filename, int line, const char* format, ...) {
	LOGINTERNAL(stdout, OPA_LOGSTRPRE, func, filename, line, format);
}

void opacoreLogErrf(const char* func, const char* filename, int line, const char* format, ...) {
	LOGINTERNAL(stderr, OPA_ERRSTRPRE, func, filename, line, format);
}

ATTR_NORETURN void opacorePanicf(const char* func, const char* filename, int line, const char* format, ...) {
	LOGINTERNAL(stderr, OPA_ERRSTRPRE, func, filename, line, format);

	// TODO*: make sure all data is written before exiting from panic!
	*((char*)-1) = 'x';
	//exit(EXIT_FAILURE);
	abort();
}

void opacoreLog(const char* func, const char* filename, int line, const char* s) {
	opacoreLogf(func, filename, line, "%s", s);
}

void opacoreLogErr(const char* func, const char* filename, int line, const char* s) {
	opacoreLogErrf(func, filename, line, "%s", s);
}

ATTR_NORETURN void opacorePanic(const char* func, const char* filename, int line, const char* s) {
	opacorePanicf(func, filename, line, "%s", s);
}

void opacoreLogStrerr(const char* func, const char* filename, int line, int errnum) {
	#ifdef _WIN32
		const char* msg = "cannot determine err string in strerror_s()";
		char tmpbuff[TMPBUFFLEN];
		if (strerror_s(tmpbuff, sizeof(tmpbuff), errnum) == 0) {
			// success
			msg = tmpbuff;
		}
	#elif defined(OPA_USE_SYS_ERRLIST)
		const char* msg = (errnum > 0 && errnum < sys_nerr) ? sys_errlist[errnum] : "err code out of range";
	#else
		const char* msg = "cannot determine err string in strerror_r()";
		char tmpbuff[TMPBUFFLEN];
		// strerror_r can have a different return type depending on which feature test macros are defined
		#if (defined(__linux__) && !((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !defined(_GNU_SOURCE)))
			msg = strerror_r(errnum, tmpbuff, sizeof(tmpbuff));
		#else
			if (strerror_r(errnum, tmpbuff, sizeof(tmpbuff)) == 0) {
				// success
				msg = tmpbuff;
			}
		#endif
	#endif

	opacoreLogErrf(func, filename, line, "system error %d; %s", errnum, msg);
}

#ifdef _WIN32
#define strrev _strrev
#else
static void strrev(char* s) {
	char* e = s + strlen(s) - 1;
	for (; s < e; ++s, --e) {
		char tmp = *s;
		*s = *e;
		*e = tmp;
	}
}
#endif

void u32toa(uint32_t v, char* s, int base) {
	OASSERT(base >= 2 && base <= 36);
	char* pos = s;
	do {
		int d = v % base;
		v = v / base;
		if (d < 10) {
			*pos = '0' + (char)d;
		} else {
			*pos = 'a' + (char)(d - 10);
		}
		pos++;
	} while (v > 0);
	*pos = 0;
	strrev(s);
}

void i32toa(int32_t v, char* s, int base) {
	if (v < 0) {
		*s = '-';
		uint32_t uv = (v == INT32_MIN) ? ((uint32_t)INT32_MAX) + 1 : (uint32_t)0 - (uint32_t)v;
		u32toa(uv, s + 1, base);
	} else {
		u32toa(v, s, base);
	}
}

void u64toa(uint64_t v, char* s, int base) {
	OASSERT(base >= 2 && base <= 36);
	char* pos = s;
	do {
		int d = v % base;
		v = v / base;
		if (d < 10) {
			*pos = '0' + (char)d;
		} else {
			*pos = 'a' + (char)(d - 10);
		}
		pos++;
	} while (v > 0);
	*pos = 0;
	strrev(s);
}

void i64toa(int64_t v, char* s, int base) {
	if (v < 0) {
		*s = '-';
		uint64_t uv = (v == INT64_MIN) ? ((uint64_t)INT64_MAX) + 1 : (uint64_t)0 - (uint64_t)v;
		u64toa(uv, s + 1, base);
	} else {
		u64toa(v, s, base);
	}
}

/*
size_t strlcpy(char* dst, const char* src, size_t maxlen) {
	if (maxlen == 0) {
		return 0;
	}
	const char* start = src;
	const char* stop = src + maxlen - 1;
	while (src < stop && *src != 0) {
		*dst++ = *src++;
	}
	*dst = 0;
	return src - start;
}

size_t strlcat(char* dst, const char* src, size_t maxlen) {
	size_t len = strnlen(dst, maxlen);
	return strlcpy(dst + len, src, maxlen - len);
}
*/

void* memdup(const void* src, size_t len) {
	void* copy = OPAMALLOC(len);
	if (copy != NULL) {
		memcpy(copy, src, len);
	}
	return copy;
}


/*
TODO: look into using prefix varint
https://github.com/WebAssembly/design/issues/601
https://news.ycombinator.com/item?id=11263378
https://github.com/stoklund/varint

size_t opaviGetStoredLen(const uint8_t* buff) {
	return __builtin_ctz(*buff | 0x100) + 1;
}
*/

size_t opaviGetStoredLen(const uint8_t* buff) {
	const uint8_t* start = buff;
	while (*buff & 0x80) {
		++buff;
	}
	return buff - start + 1;
}

int opaviLoadWithErr(const uint8_t* buff, uint64_t* pVal, const uint8_t** pBuff) {
	uint64_t val = buff[0] & 0x7F;
	int bitShift = 7;
	while ((buff[0] & 0x80) && bitShift <= 63) {
		val |= (((uint64_t)(*++buff & 0x7F)) << bitShift);
		bitShift += 7;
	}
	if (bitShift == 70 && (buff[0] & 0xFE) != 0) {
		// varint too big
		return OPA_ERR_INVARG;
	} else if (bitShift > 7 && buff[0] == 0) {
		// invalid varint MSB
		return OPA_ERR_INVARG;
	}
	*pVal = val;
	if (pBuff != NULL) {
		*pBuff = buff + 1;
	}
	return 0;
}

uint64_t opaviLoad(const uint8_t* buff, const uint8_t** pBuff) {
	uint64_t val;
	int err = opaviLoadWithErr(buff, &val, pBuff);
	if (err) {
		OPAPANICF("invalid varint %d", err);
	}
	return val;
}

uint8_t* opaviStore(uint64_t val, uint8_t* buff) {
	while (val > 0x7F) {
		*buff++ = 0x80 | (val & 0x7F);
		val >>= 7;
	}
	*buff++ = val & 0x7F;
	return buff;
}

uint8_t opaviStoreLen(uint64_t val) {
	uint8_t len = 1;
	while (val > 0x7F) {
		++len;
		val >>= 7;
	}
	return len;

	//return 1 + (((64 - __builtin_clzll(val | 0x01)) - 1) / 7);
}

static int isDigit(char c) {
	return c <= '9' && c >= '0';
}

int opaIsNumStr(const char* s, const char* end) {
	// first char must be '-' or digit; if first is '-' then 2nd must be digit
	if (s < end && *s == '-') {
		++s;
	}
	if (s >= end || !isDigit(*s)) {
		return 0;
	}
	++s;
	const char* decPos = NULL;
	const char* epos = NULL;
	while (s < end) {
		char ch = *s;
		if (!isDigit(ch)) {
			if (epos == NULL && (ch == 'e' || ch == 'E')) {
				// 1 'e' or 'E' is allowed
				epos = s;
				if (s + 1 < end && (s[1] == '-' || s[1] == '+')) {
					// '+' or '-' is allowed after 'e' or 'E'; skip if present
					++s;
				}
				if (s + 1 >= end) {
					// e or E must be followed by a digit
					return 0;
				}
			} else if (epos == NULL && decPos == NULL && ch == '.') {
				// 1 '.' is allowed and it must come before the 'E'
				decPos = s;
				if (s + 1 >= end) {
					// if a decimal is present, then it must be followed by a digit
					return 0;
				}
			} else {
				return 0;
			}
		}
		++s;
	}
	return 1;
}

const uint8_t* opaFindInvalidUtf8(const uint8_t* s, size_t len) {
	const uint8_t* end = s + len;

	// TODO: look into other techniques to speed this up
	//  https://github.com/lemire/fastvalidate-utf-8
	//  https://github.com/cyb70289/utf8/

	/*
	#ifdef __SSE2__
		// use SSE to skip ascii chars
		if (len >= 16) {
			const uint8_t* sseEnd = end - 16;
			while (s < sseEnd) {
				// skip in chunks of 16 bytes if MSB is not set (chars are ASCII)
				int mask = _mm_movemask_epi8(_mm_loadu_si128((const __m128i*) s));
				if (mask != 0) {
					// this chunk contains a non-ascii character
					s += __builtin_ctz(mask);
					break;
				}
				s += 16;
			}
		}
	#endif
	*/

	// the following code is adapted from https://www.cl.cam.ac.uk/~mgk25/ucs/utf8_check.c
	//   Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/> -- 2005-03-30
	//   License: http://www.cl.cam.ac.uk/~mgk25/short-license.html
	while (s < end) {
		if (*s < 0x80) {
			s++;
		} else if ((s[0] & 0xe0) == 0xc0 && s + 1 < end) {
			// 110XXXXx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 || (s[0] & 0xfe) == 0xc0) {
				return s;
			}
			s += 2;
		} else if ((s[0] & 0xf0) == 0xe0 && s + 2 < end) {
			// 1110XXXX 10Xxxxxx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 ||
				(s[2] & 0xc0) != 0x80 ||
				(s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) ||  // overlong?
				(s[0] == 0xed && (s[1] & 0xe0) == 0xa0) ||  // surrogate?
				(s[0] == 0xef && s[1] == 0xbf &&
				(s[2] & 0xfe) == 0xbe)) {                   // U+FFFE or U+FFFF?
				return s;
			}
			s += 3;
		} else if ((s[0] & 0xf8) == 0xf0 && s + 3 < end) {
			// 11110XXX 10XXxxxx 10xxxxxx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 ||
				(s[2] & 0xc0) != 0x80 ||
				(s[3] & 0xc0) != 0x80 ||
				(s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) ||      // overlong?
				(s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4) { // > U+10FFFF?
				return s;
			}
			s += 4;
		} else {
			return s;
		}
	}
	return NULL;
}
