/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef __linux__
#define _POSIX_C_SOURCE 200808L // ftello fseeko
#define _GNU_SOURCE             // strerror_r
#elif defined __unix__
#define _POSIX_C_SOURCE 200808L // ftello fseeko
#endif

#ifdef _WIN32
	#include <windows.h>
	#include <io.h>
	// note: windows does not have flockfile or funlockfile
	//   _lock_file and _unlock_file exist but may not be supported in older versions of windows?
	//#define flockfile _lock_file
	//#define funlockfile _unlock_file
#else
	#include <sys/time.h>
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winutils.h"

#include "opacore.h"


#ifdef _WIN32
#define fopen winfopen
// fseeko/ftello/off_t are defined when compiling with mingw and _FILE_OFFSET_BITS=64 (see /usr/share/mingw-w64/include/stdio.h)
#ifndef ftello
#define fseeko _fseeki64
#define ftello _ftelli64
#define off_t __int64
#endif
#endif


#define TMPBUFFLEN 512


#ifdef _WIN32

OPA_ATTR_PRINTF(3, 0)
static int vsnprintf_copyva(char* buf, size_t len, const char* format, va_list ap) {
	va_list ap2;
	va_copy(ap2, ap);
	int result = vsnprintf(buf, len, format, ap2);
	va_end(ap2);
	return result;
}

// try to print to provided buffer or allocate a new buffer if provided buffer is too small
// return pointer to provided buffer if it's large enough; else an allocated buffer that must be free'd
// return NULL on failure
// this is needed for windows because win2k msvcrt/vsnprintf does not return the required buffer length
OPA_ATTR_PRINTF(3, 0)
static char* winAllocPrintf(char* buff, size_t buffLen, const char* format, va_list ap) {
	// first try to use provided buffer to avoid allocation
	int reqLen = vsnprintf_copyva(buff, buffLen, format, ap);
	if (reqLen >= 0) {
		if ((size_t) reqLen < buffLen) {
			// success using provided buffer
			return buff;
		}
		// buffer is too small but the required length was returned (not including null char)
		buff = OPAMALLOC(++reqLen);
		if (buff != NULL) {
			if (vsnprintf_copyva(buff, reqLen, format, ap) == reqLen - 1) {
				return buff;
			}
			OPAFREE(buff);
		}
		return NULL;
	}

	// vsnprintf did not return the required allocation length. Therefore it's time to brute force
	// the length. Keep trying vsnprintf() with double the buffer size for each attempt.
	static const size_t MAX_ALLOC = 1024 * 1024 * 16;
	buffLen = buffLen > 0 ? buffLen : 256;
	buff = NULL;
	while (buffLen <= MAX_ALLOC / 2) {
		buffLen = buffLen * 2;
		char* newBuff = buff == NULL ? OPAMALLOC(buffLen) : OPAREALLOC(buff, buffLen);
		if (newBuff == NULL) {
			break;
		}
		buff = newBuff;
		reqLen = vsnprintf_copyva(buff, buffLen, format, ap);
		if (reqLen >= 0 && ((size_t)reqLen) + 1 <= buffLen) {
			return buff;
		}
	}
	OPAFREE(buff);
	return NULL;
}

OPA_ATTR_PRINTF(2, 0)
static int opa_vfprintf(FILE* f, const char* format, va_list ap) {
	int fd = _fileno(f);
	if (_isatty(fd)) {
		// logging to console: use WriteConsoleW() to preserve unicode strings
		intptr_t hi = _get_osfhandle(fd);
		HANDLE h = (HANDLE) hi;

		int retVal = -1;
		char stackbuf[TMPBUFFLEN];
		char* allocBuff = winAllocPrintf(stackbuf, sizeof(stackbuf), format, ap);
		if (allocBuff != NULL) {
			wchar_t* wstr = NULL;
			int err = winUtf8ToWide(allocBuff, &wstr);
			if (!err) {
				// TODO: lock mutex to join fflush() and WriteConsoleW() into a single atomic operation? use flockfile/_lock_file? are those functions available somewhere in win2k?
				//   probably shouldn't worry too much since stdout and stderr are probably both going to console and locking/flushing them in an atomic manner will be difficult?
				DWORD numWrittenToConsole = 0;
				fflush(f);
				if (WriteConsoleW(h, wstr, wcslen(wstr), &numWrittenToConsole, NULL)) {
					retVal = numWrittenToConsole;
				}
				OPAFREE(wstr);
			}
			if (allocBuff != stackbuf) {
				OPAFREE(allocBuff);
			}
		}
		return retVal;
	} else {
		// not logging to console - no conversion necessary
		return vfprintf(f, format, ap);
	}
}

uint64_t opaTimeMillis(void) {
	// https://stackoverflow.com/questions/1695288/getting-the-current-time-in-milliseconds-from-the-system-clock-in-windows
	FILETIME t;
	GetSystemTimeAsFileTime(&t);
	return (((((uint64_t) t.dwHighDateTime) << 32) | ((uint64_t)t.dwLowDateTime)) - 116444736000000000ULL) / 10000ULL;
}

static int isAscii(const char* s) {
	for (; *s != 0; ++s) {
		if (*s < 0) {
			return 0;
		}
	}
	return 1;
}

void opacoreLogWinErrCode(const char* func, const char* filename, int line, DWORD errnum) {
	// TODO: have FormatMessage() allocate buffer to reduce stack size required? error cases should be infrequent so performance would not be affected
	filename = opaBasename(filename);
	int success = 0;
	wchar_t tmpbuff[TMPBUFFLEN];
	if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errnum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), tmpbuff, sizeof(tmpbuff) / sizeof(tmpbuff[0]), NULL) > 0) {
		char* utf8Msg = NULL;
		int ignoreErr = winWideToUtf8(tmpbuff, &utf8Msg);
		if (!ignoreErr) {
			size_t len = strlen(utf8Msg);
			if (len > 0 && utf8Msg[len - 1] == '\n') {
				utf8Msg[--len] = 0;
			}
			if (opa_fprintf(stderr, "%s(%s:%d): win32 err %lu; %s\n", func, filename, line, errnum, utf8Msg) >= 0) {
				success = 1;
			}
			OPAFREE(utf8Msg);
			return;
		}
	}
	if (!success) {
		// try to log error code without allocating memory
		int utf8ok = !_isatty(_fileno(stderr));
		if (utf8ok || isAscii(filename)) {
			func = (utf8ok || isAscii(func)) ? func : "";
			fprintf(stderr, "%s(%s:%d): win32 err %lu\n", func, filename, line, errnum);
		} else {
			fprintf(stderr, "win32 err %lu\n", errnum);
		}
	}
}

#else

uint64_t opaTimeMillis(void) {
	struct timeval t;
	if (gettimeofday(&t, NULL)) {
		LOGSYSERRNO();
		return 0;
	}
	return (t.tv_sec * 1000) + (t.tv_usec / 1000);
}

void opaszmem(void* s, size_t n) {
	// TODO: is this correct?
	// http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
	static void* (* const volatile memsetFunc)(void*, int, size_t) = memset;
	(memsetFunc)(s, 0, n);
}

OPA_ATTR_PRINTF(2, 0)
static int opa_vfprintf(FILE* f, const char* format, va_list ap) {
	return vfprintf(f, format, ap);
}

#endif


const char* opaBasename(const char* file) {
#ifdef _WIN32
	const char* pos = strrchr(file, '/');
	const char* pos2 = strrchr(file, '\\');
	if (pos2 != NULL && (pos == NULL || pos2 > pos)) {
		pos = pos2;
	}
	return pos == NULL ? file : pos + 1;
#else
	const char* pos = strrchr(file, '/');
	return pos == NULL ? file : pos + 1;
#endif
}

int opa_fprintf(FILE* f, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int result = opa_vfprintf(f, format, args);
	va_end(args);
	return result;
}

int opa_printf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	int result = opa_vfprintf(stdout, format, args);
	va_end(args);
	return result;
}

OPA_ATTR_PRINTF(3, 0)
static int opa_vsnprintf(char* buff, size_t buffLen, const char* format, va_list args) {
	int result = -1;
	if (format != NULL && buffLen <= (size_t)INT_MAX) {
		result = vsnprintf(buff, buffLen, format, args);
	}
	if (buff != NULL && buffLen > 0) {
		if (result < 0) {
			buff[0] = 0;
		} else if ((size_t)result >= buffLen) {
			buff[buffLen - 1] = 0;
		}
	}
	return result;
}

int opa_snprintf(char* buff, size_t buffLen, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int result = opa_vsnprintf(buff, buffLen, format, args);
	va_end(args);
	return result;
}

static void opacoreLogInternal2(FILE* f, const char* func, const char* filename, int line, const char* msg) {
	filename = opaBasename(filename);
#ifdef _WIN32
	int res = opa_fprintf(f, "%s(%s:%d): %s\n", func, filename, line, msg);
	if (res < 0) {
		int utf8ok = !_isatty(_fileno(f));
		if (utf8ok || isAscii(filename)) {
			func = (utf8ok || isAscii(func)) ? func : "";
			fprintf(f, "%s(%s:%d): %s\n", func, filename, line, msg);
		} else {
			fprintf(f, "%s\n", msg);
		}
	}
#else
	fprintf(f, "%s(%s:%d): %s\n", func, filename, line, msg);
#endif
}

OPA_ATTR_PRINTF(5, 0)
static void opacoreLogInternal(FILE* f, const char* func, const char* filename, int line, const char* format, va_list args) {
	filename = opaBasename(filename);
#ifdef _WIN32
	char tmp[TMPBUFFLEN];
	char* allocBuff = winAllocPrintf(tmp, sizeof(tmp), format, args);
	if (allocBuff != NULL) {
		opacoreLogInternal2(f, func, filename, line, allocBuff);
		if (allocBuff != tmp) {
			OPAFREE(allocBuff);
		}
	} else {
		opacoreLogInternal2(f, func, filename, line, "<winAllocPrintf() failed>");
	}
#else
	flockfile(f);
	fprintf(f, "%s(%s:%d): ", func, filename, line);
	vfprintf(f, format, args);
	fputc('\n', f);
	funlockfile(f);
#endif
}

#ifdef OPADBG
void opacoreLogFFLF(const char* func, const char* filename, int line, const char* format, ...) {
	va_list args;
	va_start(args, format);
	opacoreLogInternal(stdout, func, filename, line, format, args);
	va_end(args);
}
#else
void opacoreLog (const char* s) {
	opa_fprintf(stdout, "%s\n", s);
}
#endif

void opacoreLogErrf(const char* func, const char* filename, int line, const char* format, ...) {
	va_list args;
	va_start(args, format);
	opacoreLogInternal(stderr, func, filename, line, format, args);
	va_end(args);
}

ATTR_NORETURN static void opacorePanicInternal(void) {
	// TODO: use abort instead? avoid signal handler? print stack trace? https://github.com/redis/redis/pull/7585
	// TODO*: make sure all data is written before exiting from panic!
	*((char*)-1) = 'x';
	//exit(EXIT_FAILURE);
	abort();
}

ATTR_NORETURN void opacorePanicf(const char* func, const char* filename, int line, const char* format, ...) {
	va_list args;
	va_start(args, format);
	opacoreLogInternal(stderr, func, filename, line, format, args);
	va_end(args);
	opacorePanicInternal();
}

void opacoreLogErr(const char* func, const char* filename, int line, const char* s) {
	opacoreLogInternal2(stderr, func, filename, line, s);
}

ATTR_NORETURN void opacorePanic(const char* func, const char* filename, int line, const char* s) {
	opacoreLogInternal2(stderr, func, filename, line, s);
	opacorePanicInternal();
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

#ifdef OPACOVTEST
void opacoreCovTestAssert(const char* func, const char* filename, int line, int v, const char* s) {
	if (!v) {
		opacorePanicf(func, filename, line, "assertion '%s' failed", s);
	}
}
#endif

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


/*
TODO: look into using prefix varint
https://github.com/WebAssembly/design/issues/601
https://news.ycombinator.com/item?id=11263378
https://github.com/stoklund/varint

size_t opaviGetStoredLen(const uint8_t* buff) {
	return __builtin_ctz(*buff | 0x100) + 1;
}
*/

void opaZeroAndFree(void* ptr, size_t len) {
	if (ptr != NULL) {
		opaszmem(ptr, len);
		OPAFREE(ptr);
	}
}

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

static int isinfstrInternal(const char* b, size_t len) {
	const char* a = "infinity";
	OASSERT(len > 0 && len <= strlen(a));
	for (; len > 0; --len, ++a, ++b) {
		if (*a != opaToLowerAscii(*b)) {
			return 0;
		}
	}
	return 1;
}

int opaIsInfStr(const char* str, size_t len) {
	switch (len) {
		case 9:
		case 4:
			if (str[0] == '-' && isinfstrInternal(str + 1, len - 1)) {
				return -1;
			} else if (str[0] == '+' && isinfstrInternal(str + 1, len - 1)) {
				return 1;
			}
			break;
		case 8:
		case 3:
			if (isinfstrInternal(str, len)) {
				return 1;
			}
			break;
	}
	return 0;
}

const uint8_t* opaFindInvalidUtf8(const uint8_t* s, size_t len) {
	const uint8_t* end = s + len;

	// TODO: look into other techniques to speed this up
	//  https://github.com/lemire/fastvalidate-utf-8
	//  https://github.com/cyb70289/utf8/
	//  https://lemire.me/blog/2020/10/20/ridiculously-fast-unicode-utf-8-validation/

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
				(s[0] == 0xed && (s[1] & 0xe0) == 0xa0)) {  // surrogate?
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

#ifndef OPA_NO_LOWER_LUT
const unsigned char LOWER_LUT[256] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
	0x40,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x5b,0x5c,0x5d,0x5e,0x5f,
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
	0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
	0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
	0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
	0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
	0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
	0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
	0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
	0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};
#endif

char opaToLowerAscii(char ch) {
#ifdef OPA_NO_LOWER_LUT
	return ch <= 'Z' && ch >= 'A' ? 'a' + (ch - 'A') : ch;
#else
	return LOWER_LUT[(unsigned char) ch];
#endif
}

int opaStrCmpNoCaseAscii(const char* s1, const char* s2) {
	// TODO: faster SIMD/SSE implementation?
	while (1) {
		char ch1 = opaToLowerAscii(*s1++);
		char ch2 = opaToLowerAscii(*s2++);
		if (ch1 != ch2 || ch1 == 0) {
			return ch1 - ch2;
		}
	}
}

int opaStrCmpNoCaseAsciiLen(const void* v1, size_t l1, const void* v2, size_t l2) {
	// TODO: faster SIMD/SSE implementation?
	const char* s1 = v1;
	const char* s2 = v2;
	const char* stop = l1 < l2 ? s1 + l1 : s1 + l2;
	while (s1 < stop) {
		char ch1 = opaToLowerAscii(*s1++);
		char ch2 = opaToLowerAscii(*s2++);
		if (ch1 != ch2) {
			return ch1 - ch2;
		}
	}
	return l1 > l2 ? 1 : (l1 < l2 ? -1 : 0);
}

/*
void opacorePrintLowerLUT() {
	char lut[256];
	for (int ch = 0; ch < sizeof(lut); ++ch) {
		lut[ch] = ch;
	}
	for (int ch = 'A'; ch <= 'Z'; ++ch) {
		lut[ch] = 'a' + (ch - 'A');
	}
	for (size_t i = 0; i < sizeof(lut); ++i) {
		printf("0x%02hhx,", lut[i]);
		if (i % 16 == 15) {
			printf("\n");
		}
	}
}
*/

// read a file into a null terminated string buffer
int opacoreReadFile(const char* path, uint8_t** pBuff, size_t* pLen) {
	uint8_t* buff = NULL;
	off_t len = 0;
	off_t totRead = 0;
	FILE* f = fopen(path, "rb");
	if (f == NULL) {
		goto err;
	}
	if (fseeko(f, 0, SEEK_END) != 0) {
		goto err;
	}
	len = ftello(f);
	if (len < 0) {
		goto err;
	}
	rewind(f);

	buff = OPAMALLOC(len + 1);
	if (buff == NULL) {
		goto err;
	}
	while (totRead < len) {
		totRead += fread(buff + totRead, 1, len - totRead, f);
		if (ferror(f)) {
			goto err;
		}
	}
	fclose(f);
	buff[len] = 0;
	*pBuff = buff;
	*pLen = len;
	return 0;

	err:
	LOGSYSERRNO();
	if (f != NULL) {
		fclose(f);
	}
	if (buff != NULL && len > 0) {
		opaZeroAndFree(buff, len);
	}
	return OPA_ERR_INTERNAL;
}
