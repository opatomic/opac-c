#include "opac.h"

#ifndef OPAC_VERSION
#define OPAC_VERSION "0.0.0-dev"
#endif

#ifdef OPA_NOTHREADS
#define OPAC_BITHREADS 0
#else
#define OPAC_BITHREADS 1
#endif

// TODO: add version info for bigint libraries
//    see: gmp_version __GNU_MP_VERSION __GNU_MP_VERSION_MINOR __GNU_MP_VERSION_PATCH
#ifdef OPA_USEGMP
#define OPA_MPLIB "gmp"
#else
#define OPA_MPLIB "libtommath"
#endif

const opacBuildInfo BUILDINFO = {
	OPAC_VERSION, OPAC_BITHREADS, OPA_MPLIB
};

const opacBuildInfo* opacGetBuildInfo(void) {
	return &BUILDINFO;
}
