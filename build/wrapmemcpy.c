#if defined(__GNUC__) && defined(__linux__)

#include <stddef.h>
#include <string.h>

// NOTE!! This file must be compiled with gcc lto disabled

// using memcpy adds a dependency to glibc 2.14. instead, we want to depend on glibc 2.2.5
// to use this, add -Wl,--wrap=memcpy when linking with gcc. this will tell gcc to use __wrap_memcpy instead of memcpy
// -flto does not seem to work with this hack. must disable when compiling this source file
void* __wrap_memcpy(void* restrict to, const void* restrict from, size_t size);
__asm__(".symver memcpy, memcpy@GLIBC_2.2.5");
void* __attribute__((used)) __wrap_memcpy(void* restrict to, const void* restrict from, size_t size) {
	return memcpy(to, from, size);
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif

