/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef BASE64_H_
#define BASE64_H_

#include <stddef.h>

size_t base64EncodeLen(size_t srcLen, int appendEquals);
void base64Encode(const void* src, size_t srcLen, void* dst, int appendEquals);

size_t base64DecodeLen(const void* src, size_t srcLen);
// returns 0 if an invalid char is encountered; else return 1
int base64Decode(const void* src, size_t srcLen, void* dst);

#endif
