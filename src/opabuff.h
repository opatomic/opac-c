/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPABUFF_H_
#define OPABUFF_H_

#include <stddef.h>
#include <stdint.h>

/**
 * Try to lock allocation in memory and disable paging to disk. If an error occurs
 * when locking the underlying allocation to memory, the error is silently ignored
 * (unless OPABUFF_F_MLOCKERR flag is specified).
 */
#define OPABUFF_F_NOPAGING  0x02
/**
 * Zero all used bytes when growing/shrinking/freeing data in buffer
 * note: if buffer needs to grow then it must be allocated to a new address and the
 *   original allocation is zero'd which will be slower than using realloc
 * note: if program crashes then memory will not be zero'd TODO?
 */
#define OPABUFF_F_ZERO      0x04
/**
 * Return an error if OPABUFF_F_NOPAGING is enabled and cannot lock allocation into RAM
 */
#define OPABUFF_F_MLOCKERR  0x08

typedef struct {
	uint8_t* data;
	size_t len;
	size_t cap;
	unsigned int flags;
} opabuff;

/**
 * Initialize a new buff and attempt to allocate enough memory to hold specified length.
 * Will return a zero-capacity buffer if memory allocation error occurs.
 */
opabuff opabuffNew(size_t len);

/**
 * Initialize a buff with specified flags. Pass 0 for flags to use default behavior
 */
void opabuffInit(opabuff* b, unsigned int flags);

/**
 * Get a pointer to the underlying data at specified position. Return NULL if pos is greater
 * than length of this buffer.
 */
uint8_t* opabuffGetPos(const opabuff* b, size_t pos);

/**
 * Get the length of this buffer
 */
size_t opabuffGetLen(const opabuff* b);

/**
 * Set the length of the used bytes in the buffer. If length is increasing then new bytes
 * are not initialized (can be any random value).
 * @return an error code if enough memory could not be allocated; else 0
 */
int opabuffSetLen(opabuff* b, size_t newlen);

/**
 * Append specified bytes to the end of the buffer.
 * @return an error code if enough memory could not be allocated; else 0
 */
int opabuffAppend(opabuff* b, const void* src, size_t srcLen);

/**
 * Append a single byte to end of buffer
 * @return an error code if enough memory could not be allocated; else 0
 */
int opabuffAppend1(opabuff* b, uint8_t v);

/**
 * In case buffer allocated more memory than needed: try to realloc underlying memory so that it
 * only uses enough to store the length of this buff.
 */
void opabuffRemoveFreeSpace(opabuff* b);

/**
 * Free all memory associated with this buffer. Buffer length is set to zero.
 * Buffer can be re-used.
 */
void opabuffFree(opabuff* b);


#endif
