/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include "opac.h"
#include "opabigint.h"

#define PPSTR(a) #a
#define PPXSTR(a) PPSTR(a)

#ifndef OPAC_VERSION
#define OPAC_VERSION 0.0.0-dev
#endif

#ifdef OPA_NOTHREADS
#define OPAC_BITHREADS 0
#else
#define OPAC_BITHREADS 1
#endif

// TODO: add version info for bigint libraries
//    see: gmp_version __GNU_MP_VERSION __GNU_MP_VERSION_MINOR __GNU_MP_VERSION_PATCH

const opacBuildInfo BUILDINFO = {
	PPXSTR(OPAC_VERSION), OPAC_BITHREADS, OPABIGINT_LIB_NAME
};

const opacBuildInfo* opacGetBuildInfo(void) {
	return &BUILDINFO;
}
