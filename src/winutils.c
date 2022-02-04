/*
 * Copyright 2020 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifdef _WIN32

// note: when including ntstatus.h, must define WIN32_NO_STATUS before including windows.h
//  see http://www.mschaef.com/windows_h_is_wierd
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#include <ntstatus.h>

#include "opacore.h"
#include "winutils.h"

typedef NTSTATUS (__stdcall*RtlGetVersionFunc)(RTL_OSVERSIONINFOW* lpVersionInformation);

static void winGetRealVersionInternal(winRealVerInfo* info) {
	memset(info, 0, sizeof(winRealVerInfo));
	OSVERSIONINFO gvi = {0};
	gvi.dwOSVersionInfoSize = sizeof(gvi);
	if (!GetVersionEx(&gvi)) {
		return;
	}
	info->major = gvi.dwMajorVersion;
	info->minor = gvi.dwMinorVersion;
	info->build = gvi.dwBuildNumber;
	info->platform = gvi.dwPlatformId;
	// note: GetVersionEx() is not used because it may use the embedded manifest rather than
	//   determining the true version. If possible, RtlGetVersion() is used instead.
	// https://stackoverflow.com/questions/36543301/detecting-windows-10-version/36543774#36543774
	HMODULE hlib = LoadLibrary("ntdll.dll");
	if (hlib != NULL) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
		void* func = GetProcAddress(hlib, "RtlGetVersion");
		if (func != NULL) {
			RTL_OSVERSIONINFOW osinfo;
			osinfo.dwOSVersionInfoSize = sizeof(osinfo);
			if (((RtlGetVersionFunc)func)(&osinfo) == STATUS_SUCCESS) {
				info->major = osinfo.dwMajorVersion;
				info->minor = osinfo.dwMinorVersion;
				info->build = osinfo.dwBuildNumber;
				info->platform = osinfo.dwPlatformId;
			}
		}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		FreeLibrary(hlib);
	}
}

void winGetRealVersion(winRealVerInfo* info) {
	static int infoLoaded = 0;
	static winRealVerInfo cachedInfo;
	if (!infoLoaded) {
		winGetRealVersionInternal(&cachedInfo);
		infoLoaded = 1;
	}
	memcpy(info, &cachedInfo, sizeof(cachedInfo));
}

int winIsVerGTE(DWORD major, DWORD minor) {
	winRealVerInfo ver;
	winGetRealVersion(&ver);
	return ver.major > major || (ver.major == major && ver.minor >= minor);
}

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

void usleep(unsigned long usec) {
	LARGE_INTEGER li;
	// SetWaitableTimer: negative value indicates relative time; positive value indicate absolute time
	//  value is in 100-nanosecond intervals
	li.QuadPart = 0LL - ((long long)usec * 10);
	HANDLE ht = CreateWaitableTimer(NULL, TRUE, NULL);
	if (ht != NULL) {
		if (SetWaitableTimer(ht, &li, 0, NULL, NULL, FALSE)) {
			WaitForSingleObject(ht, INFINITE);
		} else {
			LOGWINERR();
		}
		if (!CloseHandle(ht)) {
			LOGWINERR();
		}
	} else {
		LOGWINERR();
	}
}

#else

// this is here to get rid of a warning for "an empty translation unit"
typedef int compilerWarningFix;

#endif
