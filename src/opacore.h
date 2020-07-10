/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPACORE_H_
#define OPACORE_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
//#include <string.h>


#ifdef _MSC_VER
#define ATTR_NORETURN __declspec(noreturn)
#define restrict __restrict
#elif defined(__GNUC__)
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_NORETURN
#endif

//#define NONNULL(args) __attribute__ ((nonnull args))
#define UNUSED(x) (void)(x)
#define SASSERT(e) switch(0){case 0: case e:;}

#ifdef NDEBUG
#define OASSERT(c) ((void)(0))
#elif defined(OPACOVTEST)
#define OASSERT(c) opacoreCovTestAssert(OPAFUNC, __FILE__, __LINE__, (c), #c)
#elif defined(OPA_HIDEASSERT)
#define OASSERT(c) do {if (!(c)) {OPAPANIC("assertion failed");}} while(0)
#else
#define OASSERT(c) do {if (!(c)) {OPAPANICF("assertion '%s' failed", #c);}} while(0)
#endif

#if defined(OPADBG) && defined(__GNUC__)
#define list_entry(ptr, type, member) (__extension__({OASSERT(ptr != NULL); const __typeof__( ((type*)0)->member ) *_MPWT = (ptr);(type*)((void*)( (char*)_MPWT - offsetof(type, member) ));}))
#else
// note: cast to void* then to type* to avoid a -Wcast-align warning
#define list_entry(ptr, type, member) ((type*)(void*)((char*)(ptr) - offsetof(type, member)))
#endif


#ifdef OPA_NOFUNCNAMES
#define OPAFUNC ""
#else
#define OPAFUNC __func__
#endif

#ifdef _WIN32
// note: build is faster if windows header files are not included
//#include <windef.h>
typedef unsigned long DWORD;
#endif

#define OPALOGF(format, ...)    opacoreLogf   (OPAFUNC, __FILE__, __LINE__, format, __VA_ARGS__)
#define OPALOGERRF(format, ...) opacoreLogErrf(OPAFUNC, __FILE__, __LINE__, format, __VA_ARGS__)
#define OPAPANICF(format, ...)  opacorePanicf (OPAFUNC, __FILE__, __LINE__, format, __VA_ARGS__)

#define OPALOG(str)    opacoreLog   (OPAFUNC, __FILE__, __LINE__, str)
#define OPALOGERR(str) opacoreLogErr(OPAFUNC, __FILE__, __LINE__, str)
#define OPAPANIC(str)  opacorePanic (OPAFUNC, __FILE__, __LINE__, str)

#ifdef _WIN32
void opacoreLogWinErrCode(const char* func, const char* filename, int line, DWORD err);
#define LOGWINERRCODE(code) opacoreLogWinErrCode(OPAFUNC, __FILE__, __LINE__, code)
#define LOGWINERR() LOGWINERRCODE(GetLastError())
#endif

#define LOGSYSERR(errcode) opacoreLogStrerr(OPAFUNC, __FILE__, __LINE__, errcode)
#define LOGSYSERRNO() LOGSYSERR(errno)


#ifndef OPAMALLOC
	#include <stdlib.h>
	#define OPAMALLOC  malloc
	#define OPAFREE    free
	#define OPAREALLOC realloc
	#define OPACALLOC  calloc
#else
	extern void* OPAMALLOC(size_t n);
	extern void* OPAREALLOC(void* p, size_t n);
	extern void* OPACALLOC(size_t n, size_t s);
	extern void  OPAFREE(void* p);
#endif




#define OPADEF_SORTMAX      'Z'
#define OPADEF_UNDEFINED    'U'
#define OPADEF_NULL         'N'
#define OPADEF_FALSE        'F'
#define OPADEF_TRUE         'T'
#define OPADEF_BIN_EMPTY    'A'
#define OPADEF_STR_EMPTY    'R'
#define OPADEF_ARRAY_EMPTY  'M'
// TODO: add +infinity -infinity -0 NaN ?
#define OPADEF_ZERO         'O'
#define OPADEF_POSVARINT    'D'
#define OPADEF_NEGVARINT    'E'
// vardec: [varint varint] first varint is exponent; second varint is significand
#define OPADEF_POSPOSVARDEC 'G'
#define OPADEF_POSNEGVARDEC 'H'
#define OPADEF_NEGPOSVARDEC 'I'
#define OPADEF_NEGNEGVARDEC 'J'
// bigint: [varint bytes] varint specifies number of bytes to follow; bytes encodes the value
#define OPADEF_POSBIGINT    'K'
#define OPADEF_NEGBIGINT    'L'
// bigdec: [varint bigint] varint encodes exponent; bigint is the significand
#define OPADEF_POSPOSBIGDEC 'V'
#define OPADEF_POSNEGBIGDEC 'W'
#define OPADEF_NEGPOSBIGDEC 'X'
#define OPADEF_NEGNEGBIGDEC 'Y'
// blob:   [varint bytes] varint specifies number of bytes to follow
#define OPADEF_BIN_LPVI     'B'
// string: [varint bytes] varint specifies number of bytes to follow; bytes must be valid UTF-8
#define OPADEF_STR_LPVI     'S'
#define OPADEF_ARRAY_START  '['
#define OPADEF_ARRAY_END    ']'


#define OPA_ERR_INTERNAL    -1
#define OPA_ERR_NOMEM       -2
#define OPA_ERR_INVSTATE    -3
#define OPA_ERR_INVARG      -4
#define OPA_ERR_OVERFLOW    -5
#define OPA_ERR_PARSE       -6
#define OPA_ERR_WOULDBLOCK  -7
#define OPA_ERR_EOF         -8
#define OPA_ERR_UNSUPPORTED -9


#ifdef OPACOVTEST
void opacoreCovTestAssert(const char* func, const char* filename, int line, int v, const char* s);
#endif

void opacoreLog   (const char* func, const char* filename, int line, const char* s);
void opacoreLogErr(const char* func, const char* filename, int line, const char* s);
ATTR_NORETURN
void opacorePanic (const char* func, const char* filename, int line, const char* s);

#ifdef __GNUC__
	#ifdef _WIN32
		#define OPA_ATTR_PRINTF_FFLF __attribute__((__format__ (gnu_printf, 4, 5)))
	#else
		#define OPA_ATTR_PRINTF_FFLF __attribute__((__format__ (__printf__, 4, 5)))
	#endif
#else
	#define OPA_ATTR_PRINTF_FFLF
#endif
OPA_ATTR_PRINTF_FFLF
void opacoreLogf   (const char* func, const char* filename, int line, const char* format, ...);
OPA_ATTR_PRINTF_FFLF
void opacoreLogErrf(const char* func, const char* filename, int line, const char* format, ...);
OPA_ATTR_PRINTF_FFLF
ATTR_NORETURN
void opacorePanicf (const char* func, const char* filename, int line, const char* format, ...);

void opacoreLogStrerr(const char* func, const char* filename, int line, int errnum);

#ifdef _WIN32
#define opaszmem SecureZeroMemory
#else
void opaszmem(void* ptr, size_t len);
#endif

void opazeroAndFree(void* ptr, size_t len);
uint64_t opaTimeMillis(void);


#define OPAVI_MAXLEN64 10

uint64_t opaviLoad(const uint8_t* buff, const uint8_t** pBuff);
int opaviLoadWithErr(const uint8_t* buff, uint64_t* pVal, const uint8_t** pBuff);
size_t opaviGetStoredLen(const uint8_t* buff);
uint8_t* opaviStore(uint64_t val, uint8_t* buff);
uint8_t opaviStoreLen(uint64_t val);


int opaIsNumStr(const char* s, const char* end);
const uint8_t* opaFindInvalidUtf8(const uint8_t* s, size_t len);


#endif
