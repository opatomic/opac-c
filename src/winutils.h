/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef WINUTILS_H_
#define WINUTILS_H_

#ifdef _WIN32

#include <stdio.h>

typedef unsigned long DWORD;

int winIsVerGTE(DWORD major, DWORD minor);
int winUtf8ToWide(const char* utf8Str, wchar_t** pWstr);
int winWideToUtf8(const wchar_t* wstr, char** pUtf8Str);
FILE* winfopen(const char* filename, const char* opentype);

#endif

#endif
