/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPABIGDEC_PRIVATE_H_
#define OPABIGDEC_PRIVATE_H_

#include "opabigdec.h"

// the following are internal functions (do not use)
int opabigdecExtend(opabigdec* v, uint32_t amount);
int opabigdecConvertErr(int tomerr);

#endif
