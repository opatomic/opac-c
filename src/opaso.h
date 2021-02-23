/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPASO_H_
#define OPASO_H_

#include <stddef.h>
#include <stdint.h>

#include "opabuff.h"


size_t opasolen(const uint8_t* obj);
int opasoIsNumber(uint8_t type);
int opasoGetStrOrBin(const uint8_t* so, const uint8_t** pStrStart, size_t* pLen);

char* opasoStringify(const uint8_t* src, const char* space);
int opasoStringifyToBuff(const uint8_t* src, const char* space, opabuff* b);


#endif
