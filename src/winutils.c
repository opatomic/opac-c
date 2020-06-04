/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef _WIN32

#include <windows.h>

#include "opacore.h"
#include "winutils.h"

int winUtf8ToWide(const char* utf8Str, wchar_t** pWstr) {
	int reqLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
	if (reqLen == 0) {
		LOGWINERR();
		return OPA_ERR_INTERNAL;
	}
	wchar_t* wideStr = OPAMALLOC(reqLen * sizeof(wchar_t));
	if (wideStr == NULL) {
		return OPA_ERR_NOMEM;
	}
	int checkLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, reqLen);
	if (checkLen != reqLen) {
		OPAFREE(wideStr);
		OPALOGERR("MultiByteToWideChar result differs");
		return OPA_ERR_INTERNAL;
	}
	*pWstr = wideStr;
	return 0;
}

int winWideToUtf8(const wchar_t* wstr, char** pUtf8Str) {
	int reqLen = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (reqLen == 0) {
		LOGWINERR();
		return OPA_ERR_INTERNAL;
	}
	char* utf8str = OPAMALLOC(reqLen);
	if (utf8str == NULL) {
		return OPA_ERR_NOMEM;
	}
	int checkRes = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8str, reqLen, NULL, NULL);
	if (checkRes != reqLen) {
		OPAFREE(utf8str);
		OPALOGERR("WideCharToMultiByte result differs");
		return OPA_ERR_INTERNAL;
	}
	*pUtf8Str = utf8str;
	return 0;
}

FILE* winfopen(const char* filename, const char* opentype) {
	FILE* f = NULL;
	wchar_t* wname = NULL;
	wchar_t* wtype = NULL;
	int err = winUtf8ToWide(filename, &wname);
	if (!err) {
		err = winUtf8ToWide(opentype, &wtype);
	}
	if (!err) {
		f = _wfopen(wname, wtype);
	}
	OPAFREE(wtype);
	OPAFREE(wname);
	return f;
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif
