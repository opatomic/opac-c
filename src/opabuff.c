/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#define _POSIX_C_SOURCE 200112L // posix_memalign

#include <stdlib.h>
#include <string.h>

#include "opabuff.h"
#include "opacore.h"

#ifndef OPABUFF_DEFAULTLEN
#define OPABUFF_DEFAULTLEN 64
#endif

#define OPABUFF_F_MLOCKED 0x01

// TODO: use explicit_bzero in freebsd?

#ifdef _WIN32
#include <windows.h>
#define zeromem SecureZeroMemory
static int posix_memalign(void** memptr, size_t alignment, size_t size) {
	// note: possible incompatibility here: _aligned_malloc will set errno; but posix_memalign does not set errno
	*memptr = _aligned_malloc(size, alignment);
	return *memptr == NULL ? errno : 0;
}
static int mlock(const void* addr, size_t len) {
	return VirtualLock((void*) addr, len) ? 0 : -1;
}
static int munlock(const void* addr, size_t len) {
	return VirtualUnlock((void*) addr, len) ? 0 : -1;
}
#else
#include <sys/mman.h>
#include <unistd.h>
static void zeromem(void* s, size_t n) {
	// TODO: is this correct?
	// http://www.daemonology.net/blog/2014-09-04-how-to-zero-a-buffer.html
	static void* (* const volatile memsetFunc)(void*, int, size_t) = memset;
	(memsetFunc)(s, 0, n);
}
#endif

#ifdef _WIN32
#define ALIGNEDFREE _aligned_free
#else
#define ALIGNEDFREE free
#endif

static size_t pagesize(void) {
	#ifdef _WIN32
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		return si.dwPageSize;
	#else
		return (size_t) sysconf(_SC_PAGESIZE);
	#endif
}

static int opabuffResize(opabuff* b, size_t newCap) {
	void* newPtr;
	size_t newLen = b->len > newCap ? newCap : b->len;
	if (b->flags & OPABUFF_F_NOPAGING) {
		//return opabuffResizeSecure(b, newCap);
		unsigned int locked;
		// must allocate memory in such a way that it doesn't share a page with any other
		// allocations. This is because mlock/munlock operate on entire pages (ie,
		// munlock will unlock all allocations sharing the specified page in memory).
		// TODO: if using an allocation library (ie jemalloc), allow user to use a separate
		//  function here (must also change free() when freeing buffer)
		if (posix_memalign(&newPtr, pagesize(), newCap)) {
			return OPA_ERR_NOMEM;
		}
		// TODO: consider case where memory is already locked via mlockall or a small non-aligned allocation was made on this page.
		//   then when munlock is called, the page will be unlocked even tho it should remain locked? should instead check whether
		//   this page is unlocked. if so, then attempt to mlock and if successful, set a flag indicating the page should be unlocked.
		//   another alternative is to allocate entire pages so there are no other allocations on the pages (however this still has
		//   a problem if mlockall is called beforehand?)
		if (mlock(newPtr, newCap)) {
			// mlock failed. note: this can fail due to user not having proper permissions
			if (b->flags & OPABUFF_F_MLOCKERR) {
				ALIGNEDFREE(newPtr);
				return OPA_ERR_INTERNAL;
			}
			locked = 0;
		} else {
			locked = OPABUFF_F_MLOCKED;
		}
		memcpy(newPtr, b->data, newLen);
		opabuffFree(b);
		b->flags = (b->flags & ~OPABUFF_F_MLOCKED) | locked;
	} else if (b->flags & OPABUFF_F_ZERO) {
		// must make separate allocation and zero old data
		newPtr = OPAMALLOC(newCap);
		if (newPtr == NULL) {
			return OPA_ERR_NOMEM;
		}
		memcpy(newPtr, b->data, newLen);
		// at this point, there is a copy of all data so free old allocation
		opabuffFree(b);
	} else {
		newPtr = OPAREALLOC(b->data, newCap);
		if (newPtr == NULL) {
			return OPA_ERR_NOMEM;
		}
	}
	b->data = newPtr;
	b->len = newLen;
	b->cap = newCap;
	if (b->flags & OPABUFF_F_ZERO) {
		// zero unused bytes
		zeromem(b->data + b->len, b->cap - b->len);
	}
	return 0;
}

static int opabuffEnsureSpace(opabuff* b, size_t reqSpace) {
	SASSERT(OPABUFF_DEFAULTLEN >= 2);
	if (b->cap - b->len < reqSpace) {
		// grow by 1.5 * capacity
		size_t newCap = b->cap == 0 ? OPABUFF_DEFAULTLEN : b->cap + (b->cap >> 1);
		if (newCap < b->len + reqSpace) {
			newCap = b->len + reqSpace;
		}
		return opabuffResize(b, newCap);
	}
	return 0;
}

void opabuffInit(opabuff* b, unsigned int flags) {
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	b->flags = flags;
}

opabuff opabuffNew(size_t len) {
	opabuff b;
	opabuffInit(&b, 0);
	opabuffSetLen(&b, len);
	return b;
}

uint8_t* opabuffGetPos(const opabuff* b, size_t pos) {
	return pos <= b->len ? b->data + pos : NULL;
}

size_t opabuffGetLen(const opabuff* b) {
	return b->len;
}

/*
int opabuffInsert(opabuff* b, size_t offset, const void* src, size_t srcLen) {
	if (offset > b->len) {
		return OPA_ERR_INVARG;
	}
	int err = opabuffEnsureSpace(b, srcLen);
	if (!err) {
		memmove(b->data + offset + srcLen, b->data + offset, b->len - offset);
		if (src != NULL) {
			memmove(b->data + offset, src, srcLen);
		}
		b->len += srcLen;
	}
	return err;
}

int opabuffRemove(opabuff* b, size_t offset, size_t len) {
	if (offset > b->len || offset + len > b->len) {
		return OPA_ERR_INVARG;
	}
	memmove(b->data + offset, b->data + offset + len, b->len - (offset + len));
	b->len -= len;
	if (b->flags & OPABUFF_F_ZERO) {
		zeromem(b->data + b->len, len);
	}
	return 0;
}
*/

int opabuffAppend(opabuff* b, const void* src, size_t srcLen) {
	int err = opabuffEnsureSpace(b, srcLen);
	if (!err) {
		if (src != NULL) {
			// note: use memmove rather than memcpy because src bytes could be coming from within the buffer
			memmove(b->data + b->len, src, srcLen);
		}
		b->len += srcLen;
	}
	return err;
}

int opabuffAppend1(opabuff* b, uint8_t v) {
	if (b->len == b->cap) {
		int err = opabuffEnsureSpace(b, b->len + 1);
		if (err) {
			return err;
		}
	}
	b->data[b->len++] = v;
	return 0;
}

int opabuffSetLen(opabuff* b, size_t newlen) {
	if (newlen > b->len) {
		int err = opabuffEnsureSpace(b, newlen);
		if (!err) {
			b->len = newlen;
		}
		return err;
	} else {
		if (b->flags & OPABUFF_F_ZERO) {
			zeromem(b->data + newlen, b->len - newlen);
		}
		b->len = newlen;
		return 0;
	}
}

void opabuffRemoveFreeSpace(opabuff* b) {
	if (b->cap > b->len) {
		// note: ignore err (on err, buff will keep some free space)
		opabuffResize(b, b->len);
	}
}

void opabuffFree(opabuff* b) {
	if (b->flags & OPABUFF_F_ZERO) {
		zeromem(b->data, b->len);
	}
	if (b->flags & OPABUFF_F_MLOCKED) {
		munlock(b->data, b->cap);
		b->flags &= ~OPABUFF_F_MLOCKED;
	}
	if (b->flags & OPABUFF_F_NOPAGING) {
		ALIGNEDFREE(b->data);
	} else {
		OPAFREE(b->data);
	}
	b->data = NULL;
	b->len = b->cap = 0;
}