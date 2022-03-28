/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <string.h>

#include "opacore.h"
#include "opaso.h"

int opasoIsNumber(uint8_t type) {
	switch (type) {
		case OPADEF_NEGINF:
		case OPADEF_POSINF:
		case OPADEF_ZERO:
		case OPADEF_POSVARINT:
		case OPADEF_NEGVARINT:
		case OPADEF_POSBIGINT:
		case OPADEF_NEGBIGINT:
		case OPADEF_POSPOSVARDEC:
		case OPADEF_POSNEGVARDEC:
		case OPADEF_NEGPOSVARDEC:
		case OPADEF_NEGNEGVARDEC:
		case OPADEF_POSPOSBIGDEC:
		case OPADEF_POSNEGBIGDEC:
		case OPADEF_NEGPOSBIGDEC:
		case OPADEF_NEGNEGBIGDEC:
			return 1;
	}
	return 0;
}

static size_t opasoNumBytesAsVI(const uint8_t* b) {
	const uint8_t* start = b;
	uint64_t numBytes = opaviLoad(b, &b);
	if (numBytes > SIZE_MAX) {
		OPAPANIC("varint larger than SIZE_MAX");
	}
	return (b - start) + numBytes;
}

size_t opasolen(const uint8_t* obj) { // @suppress("No return")
	// TODO: another function to specify num bytes available in case of corruption or invalid data?
	switch (obj[0]) {
		case OPADEF_UNDEFINED:
		case OPADEF_NULL:
		case OPADEF_FALSE:
		case OPADEF_TRUE:
		case OPADEF_NEGINF:
		case OPADEF_POSINF:
		case OPADEF_ZERO:
		case OPADEF_SORTMAX:

		case OPADEF_BIN_EMPTY:
		case OPADEF_STR_EMPTY:
		case OPADEF_ARRAY_EMPTY:
			return 1;

		case OPADEF_POSVARINT:
		case OPADEF_NEGVARINT:
			return 1 + opaviGetStoredLen(obj + 1);

		case OPADEF_BIN_LPVI:
		case OPADEF_STR_LPVI:
		case OPADEF_POSBIGINT:
		case OPADEF_NEGBIGINT: {
			return 1 + opasoNumBytesAsVI(obj + 1);
		}

		case OPADEF_POSPOSVARDEC:
		case OPADEF_POSNEGVARDEC:
		case OPADEF_NEGPOSVARDEC:
		case OPADEF_NEGNEGVARDEC: {
			size_t expLen = opaviGetStoredLen(obj + 1);
			return 1 + expLen + opaviGetStoredLen(obj + 1 + expLen);
		}

		case OPADEF_POSPOSBIGDEC:
		case OPADEF_POSNEGBIGDEC:
		case OPADEF_NEGPOSBIGDEC:
		case OPADEF_NEGNEGBIGDEC: {
			size_t expLen = opaviGetStoredLen(obj + 1);
			return 1 + expLen + opasoNumBytesAsVI(obj + 1 + expLen);
		}

		case OPADEF_ARRAY_START: {
			const uint8_t* start = obj;
			for (++obj; *obj != OPADEF_ARRAY_END; obj += opasolen(obj)) {}
			return obj - start + 1;
		}
	}
	OPAPANICF("unknown type %d at address %p", obj[0], (void*) obj);
	//return 1;
}

int opasoGetStrOrBin(const uint8_t* so, const uint8_t** pStrStart, size_t* pLen) {
	if (*so == OPADEF_STR_LPVI || *so == OPADEF_BIN_LPVI) {
		uint64_t len;
		const uint8_t* start;
		int err = opaviLoadWithErr(so + 1, &len, &start);
		if (len > SIZE_MAX && !err) {
			err = OPA_ERR_OVERFLOW;
		}
		if (!err) {
			*pStrStart = start;
			*pLen = len;
		}
		return err;
	} else if (*so == OPADEF_STR_EMPTY || *so == OPADEF_BIN_EMPTY) {
		*pStrStart = NULL;
		*pLen = 0;
		return 0;
	}
	return OPA_ERR_INVARG;
}
