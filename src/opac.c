/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <string.h>

#include "opac.h"
#include "opacore.h"
#include "opaso.h"

#ifdef OPA_NOTHREADS
#define ATOMIC_INC64(v) (++(*(v)))
#else
#ifdef _MSC_VER
#define ATOMIC_INC64(v) InterlockedIncrement64((volatile LONG64*) (v))
#elif defined(__GNUC__)
#define ATOMIC_INC64(v) __sync_add_and_fetch((v), 1)
#endif
#endif

#ifndef OPAC_READLEN
#define OPAC_READLEN (1024 * 8)
#endif

#define OPAC_F_ISASYNC       0x01
#define OPAC_F_NORESPONSE    0x02
#define OPAC_F_QUEUEDFORSEND 0x04
#define OPAC_F_SENT          0x08
#define OPAC_F_RESPONSERECVD 0x10
#define OPAC_F_RESULTISERR   0x20

int opacReqIsSent(const opacReq* r) {
	return r->flags & OPAC_F_SENT;
}

int opacReqResponseRecvd(const opacReq* r) {
	return r->flags & OPAC_F_RESPONSERECVD;
}

int opacReqResponseIsErr(const opacReq* r) {
	return opacReqResponseRecvd(r) ? r->flags & OPAC_F_RESULTISERR : -1;
}

const uint8_t* opacReqGetResponse(const opacReq* r) {
	return opacReqResponseRecvd(r) ? r->pos : NULL;
}

void opacReqFreeResponse(opacReq* r) {
	if (opacReqResponseRecvd(r)) {
		opabuffFree(&r->rrbuff);
		r->pos = NULL;
		// TODO: clear struct here?
		//memset(r, 0, sizeof(opacReq));
	}
}

void opacReqInit(opacReq* r) {
	memset(r, 0, sizeof(opacReq));
}

void opacReqSetRequestBuff(opacReq* r, opabuff b) {
	//OASSERT(!(r->flags & OPAC_F_QUEUEDFORSEND) && !opacReqIsSent(r));
	OASSERT(r->flags == 0);
	r->rrbuff = b;
}

void opacReqFreeRequest(opacReq* r) {
	opabuffFree(&r->rrbuff);
}


static void opacHandleErr(opac* c, int err) {
	c->err = err;
	if (c->cbs->clientErr != NULL) {
		c->cbs->clientErr(c, err);
	}
}

static void opacHandleReqErr(opac* c, opacReq* r, opacReqErrReason reason, int errCode) {
	if (c->cbs->reqErr != NULL) {
		c->cbs->reqErr(c, r, reason, errCode);
	} else {
		opabuffFree(&r->rrbuff);
		r->pos = NULL;
	}
}

static opacReq* opacNextQueuedRequest(opac* c) {
	while (1) {
		opaqueueItem* qi = opaqueuePoll(&c->reqsToSend);
		if (qi == NULL) {
			return NULL;
		}
		opacReq* r = list_entry(qi, opacReq, qi);
		if (opabuffGetLen(&r->rrbuff) == 0) {
			opacHandleReqErr(c, r, OPAC_RER_INVREQ, 0);
			continue;
		}
		// note: must add request to mainReqs or asyncReqs before it is sent completely. otherwise
		//  if there are separate send/recv threads then the response could be received while send
		//  thread is paused.
		if (!(r->flags & OPAC_F_ISASYNC)) {
			if (!(r->flags & OPAC_F_NORESPONSE)) {
				opaqueuePush(&c->mainReqs, qi);
			}
		} else {
			opacReqAsync* ar = list_entry(r, opacReqAsync, rbase);
			if (!opacidmapAdd(&c->asyncReqs, &ar->idinfo)) {
				// an id is not added to idmap if it already exists in the idmap
				opacHandleReqErr(c, r, OPAC_RER_IDEXISTS, 0);
				continue;
			}
		}
		return r;
	}
}

void opacSendRequests(opac* c) {
	if (c->err || c->closed) {
		return;
	}
	opacReq* r;
	if (c->currSendReq != NULL) {
		r = c->currSendReq;
		c->currSendReq = NULL;
	} else {
		r = opacNextQueuedRequest(c);
	}
	while (r != NULL) {
		// TODO: add a buffer to minimize write calls? in case requests are tiny
		size_t numToWrite = opabuffGetPos(&r->rrbuff, opabuffGetLen(&r->rrbuff)) - r->pos;
		size_t numWritten = c->cbs->write(c, r->pos, numToWrite);
		if (numWritten == 0) {
			c->currSendReq = r;
			break;
		}
		r->pos += numWritten;
		if (numWritten == numToWrite) {
			r->flags |= OPAC_F_SENT;
			if (c->cbs->onSent != NULL) {
				c->cbs->onSent(c, r);
			} else {
				opabuffFree(&r->rrbuff);
			}
			r = opacNextQueuedRequest(c);
		}
	}
}

static int opacParseErrCode(const uint8_t* errbuff, opacRpcError* errobj) {
	uint64_t code;
	if (*errbuff == OPADEF_NEGVARINT) {
		int err = opaviLoadWithErr(errbuff + 1, &code, NULL);
		if (err || code > ((uint64_t)INT32_MAX + 1)) {
			return OPA_ERR_PARSE;
		}
		errobj->code = (int32_t) ((int64_t)0 - code);
		return 0;
	} else if (*errbuff == OPADEF_POSVARINT) {
		int err = opaviLoadWithErr(errbuff + 1, &code, NULL);
		if (err || code > INT32_MAX) {
			return OPA_ERR_PARSE;
		}
		errobj->code = (int32_t) code;
		return 0;
	} else {
		return OPA_ERR_PARSE;
	}
}

static int opacParseError(const uint8_t* errbuff, opacRpcError* errobj) {
	errobj->msg = NULL;
	errobj->msgLen = 0;
	errobj->data = NULL;

	if (*errbuff == OPADEF_ARRAY_START) {
		// first item in array must be varint code
		++errbuff;
		int err = opacParseErrCode(errbuff, errobj);
		if (!err) {
			errbuff += opasolen(errbuff);
			if (*errbuff == OPADEF_STR_LPVI) {
				uint64_t msgLen;
				err = opaviLoadWithErr(errbuff + 1, &msgLen, &errbuff);
				if (!err) {
					errobj->msg = errbuff;
					errobj->msgLen = msgLen;
					errbuff += msgLen;
				}
			} else if (*errbuff == OPADEF_STR_EMPTY) {
				++errbuff;
			} else {
				err = OPA_ERR_PARSE;
			}
		}
		if (!err && *errbuff != OPADEF_ARRAY_END) {
			errobj->data = errbuff;
			errbuff += opasolen(errbuff);
		}
		if (*errbuff != OPADEF_ARRAY_END) {
			err = OPA_ERR_PARSE;
		}
		return err;
	} else {
		// must be varint code only
		return opacParseErrCode(errbuff, errobj);
	}
}

int opacReqLoadErrObj(const opacReq* r, opacRpcError* errobj) {
	if (!opacReqResponseRecvd(r) || !(r->flags & OPAC_F_RESULTISERR)) {
		return OPA_ERR_INVSTATE;
	}
	return opacParseError(r->pos, errobj);
}

static opacReq* opacGetRequestById(opac* c, opacid id, int remove) {
	opacidmapItem* idinfo = opacidmapGet(&c->asyncReqs, id, remove);
	return idinfo == NULL ? NULL : &((list_entry(idinfo, opacReqAsync, idinfo))->rbase);
}

/*
static int opacCheckUtf8(const uint8_t* buff) {
	// TODO: also check for huge array depth and huge big int vals that exceed a configurable limit?
	size_t depth = 0;
	do {
		if (*buff == OPADEF_ARRAY_START) {
			++depth;
			++buff;
		} else if (*buff == OPADEF_ARRAY_END) {
			--depth;
			++buff;
		} else if (*buff == OPADEF_STR_LPVI) {
			uint64_t strLen = opaviLoad(buff + 1, &buff);
			if (opaFindInvalidUtf8(buff, strLen) != NULL) {
				return 0;
			}
			buff += strLen;
		} else {
			buff += opasolen(buff);
		}
	} while (depth > 0);
	return 1;
}
*/

static int opacOnResponse(opac* c) {
	const uint8_t* buff = opabuffGetPos(&c->currResponse, 0);
	if (*buff != OPADEF_ARRAY_START) {
		return OPA_ERR_PARSE;
	}
	//if (OPAC_CHECKUTF8 && !opacCheckUtf8(buff)) {
	//	// TODO: if there is a utf-8 encoding error then do not close connection? invoke callback with utf8 error?
	//	return OPA_ERR_PARSE;
	//}
	++buff;
	const uint8_t* result = buff;
	const uint8_t* errObj = NULL;
	const uint8_t* asyncId = NULL;
	buff += opasolen(buff);
	if (*buff != OPADEF_ARRAY_END) {
		errObj = buff;
		buff += opasolen(buff);
		if (*buff != OPADEF_ARRAY_END) {
			asyncId = buff;
			buff += opasolen(buff);
			if (*buff != OPADEF_ARRAY_END) {
				return OPA_ERR_PARSE;
			}
		}
	}
	if (errObj != NULL && *errObj != OPADEF_NULL) {
		if (*result != OPADEF_NULL) {
			// result or err must be null
			return OPA_ERR_PARSE;
		}
		result = NULL;
	} else {
		errObj = NULL;
	}
	if (asyncId != NULL && *asyncId == OPADEF_NULL) {
		// async id cannot be null
		return OPA_ERR_PARSE;
	}
	if (errObj != NULL) {
		// check whether error conforms to spec
		opacRpcError rpcerr;
		int err = opacParseError(errObj, &rpcerr);
		if (err) {
			return err;
		}
	}

	// at this point the server response should conform to spec

	opacReq* r = NULL;
	if (asyncId != NULL) {
		if (*asyncId == OPADEF_POSVARINT) {
			uint64_t id = opaviLoad(asyncId + 1, NULL);
			if (id <= INT64_MAX) {
				r = opacGetRequestById(c, id, 1);
			}
		} else if (*asyncId == OPADEF_NEGVARINT) {
			uint64_t id = opaviLoad(asyncId + 1, NULL);
			if (id <= INT64_MAX) {
				r = opacGetRequestById(c, 0 - id, 0);
			}
		} else if (opasoIsNumber(*asyncId)) {
			// note: according to specs, server cannot change asyncid. must respond with exact same bytes as request
		}
		if (r == NULL) {
			if (c->cbs->unknownAsyncId != NULL) {
				c->cbs->unknownAsyncId(c, c->currResponse);
			} else {
				char* idStr = opasoStringify(asyncId, NULL);
				OPALOGERRF("unknown async-id %s", idStr == NULL ? "" : idStr);
				OPAFREE(idStr);
				opabuffFree(&c->currResponse);
			}
		}
	} else {
		opaqueueItem* qi = opaqueuePoll(&c->mainReqs);
		if (qi == NULL) {
			// received a response for a request that was not made
			OPALOGERR("recv extra response");
			return OPA_ERR_PARSE;
		}
		r = list_entry(qi, opacReq, qi);
	}

	if (r != NULL) {
		r->flags |= OPAC_F_RESPONSERECVD;
		r->rrbuff = c->currResponse;
		memset(&c->currResponse, 0, sizeof(c->currResponse));
		if (errObj == NULL) {
			//r->resultIsErr = 0;
			r->pos = result;
		} else {
			r->flags |= OPAC_F_RESULTISERR;
			r->pos = errObj;
		}
		if (c->cbs->onResponse != NULL) {
			c->cbs->onResponse(c, r);
		}
	}

	return 0;
}

void opacParseResponses(opac* c) {
	SASSERT(OPAC_READLEN > 1);
	if (c->err || c->closed) {
		return;
	}
	int err = 0;
	uint8_t buff[OPAC_READLEN];

	size_t numRead = c->cbs->read(c, buff, OPAC_READLEN - 1);
	if (numRead == 0) {
		return;
	}
	buff[numRead] = 0;

	const uint8_t* pos = buff;
	const uint8_t* end;
	while (!err) {
		err = opappFindEnd(&c->pp, pos, numRead, &end, NULL);
		if (!err && end == NULL) {
			// end of response was not found
			err = opabuffAppend(&c->currResponse, pos, numRead);
			break;
		}
		if (!err) {
			OASSERT(end > pos && end <= pos + numRead);
			err = opabuffAppend(&c->currResponse, pos, end - pos);
		}
		if (!err) {
			// a complete response has been read
			err = opacOnResponse(c);
		}
		numRead -= end - pos;
		pos = end;
	}

	if (err) {
		// errors that can occur: OPA_ERR_NOMEM, OPA_ERR_PARSE
		opacHandleErr(c, err);
	}
}

static opacid opacGetAsyncId(opac* c, int persistent) {
	// TODO: detect/prevent overflow of id?
	uint64_t id = ATOMIC_INC64(&c->currId);
	return persistent ? 0 - id : id;
}

int opacRemovePersistent(opac* c, opacReqAsync* r) {
	opacidmapItem* i = opacidmapGet(&c->asyncReqs, r->idinfo.id, 1);
	return i != NULL;
}

/*
static const uint8_t* opacReqFindAsyncId(const opacReq* r) {
	if (r->rrbuff.len > 0) {
		const uint8_t* pos = r->rrbuff.data;
		if (*pos == OPADEF_ARRAY_START) {
			++pos;
			if (*pos != OPADEF_ARRAY_END) {
				// skip command
				pos += opasolen(pos);
				if (*pos != OPADEF_ARRAY_END) {
					// skip args
					pos += opasolen(pos);
					if (*pos != OPADEF_ARRAY_END) {
						return pos;
					}
				}
			}
		}
	}
	return NULL;
}
*/

static void opacQueueRequestInternal(opac* c, opacReq* r) {
	OASSERT(!opacReqIsSent(r) && !opacReqResponseRecvd(r) && !(r->flags & OPAC_F_RESULTISERR));
	/*
#ifdef OPAC_CHECKREQAID
	const uint8_t* apos = opacReqFindAsyncId(r);
	if (r->flags & OPAC_F_ISASYNC) {
		OASSERT(apos != NULL && (*apos == OPADEF_NEGVARINT || *apos == OPADEF_POSVARINT));
	} else if (r->flags & OPAC_F_NORESPONSE) {
		OASSERT(apos != NULL && *apos == OPADEF_NULL);
	} else {
		OASSERT(apos == NULL);
	}
#endif
*/

	if (c->err || c->closed) {
		opacHandleReqErr(c, r, OPAC_RER_CLOSED, 0);
		return;
	}
	r->pos = opabuffGetPos(&r->rrbuff, 0);
	r->flags |= OPAC_F_QUEUEDFORSEND;
	opaqueuePush(&c->reqsToSend, &r->qi);
}

void opacQueueRequest(opac* c, opacReq* r) {
	OASSERT(!(r->flags & OPAC_F_ISASYNC) && !(r->flags & OPAC_F_NORESPONSE));
	opacQueueRequestInternal(c, r);
}

static int opacAddAsyncId(opabuff* b, const void* idBuff, size_t idLen) {
	int err = 0;
	int addArgs = 0;
	size_t blen = opabuffGetLen(b);
	const uint8_t* checkPos = opabuffGetPos(b, 0);
	if (!err && !(blen > 2 && checkPos[0] == OPADEF_ARRAY_START && checkPos[1] != OPADEF_ARRAY_END && checkPos[blen - 1] == OPADEF_ARRAY_END)) {
		err = OPA_ERR_INVARG;
	}
	if (!err) {
		// skip first byte and command
		checkPos += opasolen(checkPos + 1);
		if (*checkPos == OPADEF_ARRAY_END) {
			// no args in buffer (just a command) - must add args before asyncid
			addArgs = 1;
		}
	}
	if (!err) {
		err = opabuffAppend(b, NULL, addArgs + idLen);
	}
	if (!err) {
		// must get buffer pointer after opabuffAppend() since it might change due to reallocation
		uint8_t* pos = opabuffGetPos(b, blen - 1);
		if (addArgs) {
			*pos++ = OPADEF_NULL;
		}
		memcpy(pos, idBuff, idLen);
		pos += idLen;
		*pos = OPADEF_ARRAY_END;
	}
	return err;
}

void opacQueueNoResponseRequest(opac* c, opacReq* r) {
	const uint8_t id[] = {OPADEF_NULL};
	int err = opacAddAsyncId(&r->rrbuff, id, 1);
	if (!err) {
		r->flags |= OPAC_F_NORESPONSE;
		opacQueueRequestInternal(c, r);
	} else {
		opacHandleReqErr(c, r, OPAC_RER_ERR, err);
	}
}

void opacQueueAsyncRequest(opac* c, opacReqAsync* r, int persistent) {
	// TODO: could assign (and append) asyncid when request is dequeued in opacSendRequests(); if
	//  opacSendRequests() is only called by 1 thread then this could have some advantages: the
	//  id wouldn't need an atomic increment and would be easier to check for overflow
	uint8_t idBuff[1 + OPAVI_MAXLEN64];
	opacid id = opacGetAsyncId(c, persistent);
	OASSERT(id > INT64_MIN && id != 0);
	if (id < 0) {
		idBuff[0] = OPADEF_NEGVARINT;
		opaviStore(0 - id, idBuff + 1);
	} else {
		idBuff[0] = OPADEF_POSVARINT;
		opaviStore(id, idBuff + 1);
	}

	int err = opacAddAsyncId(&r->rbase.rrbuff, idBuff, opasolen(idBuff));
	if (!err) {
		r->idinfo.id = id;
		r->rbase.flags |= OPAC_F_ISASYNC;
		opacQueueRequestInternal(c, &r->rbase);
	} else {
		opacHandleReqErr(c,  &r->rbase, OPAC_RER_ERR, err);
	}
}

static void opacInitInternal(opac* c, const opacFuncs* funcs) {
	OASSERT(funcs != NULL && funcs->read != NULL && funcs->write != NULL);
	memset(c, 0, sizeof(opac));
	c->cbs = funcs;
}

void opacInit(opac* c, const opacFuncs* funcs) {
	opacInitInternal(c, funcs);
	opaqueueInit(&c->reqsToSend);
	opaqueueInit(&c->mainReqs);
	opacidmapInit(&c->asyncReqs);
}

int opacIsOpen(opac* c) {
	return !c->closed && c->err == 0;
}

#ifndef OPA_NOTHREADS
void opacInitMT(opac* c, const opacFuncs* funcs) {
	opacInitInternal(c, funcs);
	opaqueueInitMT(&c->reqsToSend);
	opaqueueInitMT(&c->mainReqs);
	opacidmapInitMT(&c->asyncReqs);
}
#endif

static void opacCloseReq(opac* c, opacReq* r) {
	if (r == c->currSendReq) {
		// should have already called opacHandleReqErr() for this
		return;
	}
	OASSERT(opacReqIsSent(r));
	opacHandleReqErr(c, r, OPAC_RER_CLOSED, 0);
}

static void opacCloseAsyncCB(void* c, const opacidmapItem* i) {
	opacReqAsync* r = list_entry(i, opacReqAsync, idinfo);
	opacCloseReq((opac*) c, &r->rbase);
}

void opacClose(opac* c) {
	if (c->closed) {
		return;
	}
	c->closed = 1;

	// note: c->currSendReq is added to mainReqs or asyncReqs (or neither if has NULL asyncid)
	//  therefore, it is important to remember this in opacCloseReq() above
	if (c->currSendReq != NULL) {
		opacHandleReqErr(c,  c->currSendReq, OPAC_RER_CLOSED, 0);
	}

	while (1) {
		opaqueueItem* qi = opaqueuePoll(&c->reqsToSend);
		if (qi == NULL) {
			break;
		}
		opacHandleReqErr(c,  list_entry(qi, opacReq, qi), OPAC_RER_CLOSED, 0);
	}

	while (1) {
		opaqueueItem* qi = opaqueuePoll(&c->mainReqs);
		if (qi == NULL) {
			break;
		}
		opacReq* r = list_entry(qi, opacReq, qi);
		opacCloseReq(c, r);
	}

	opacidmapIterate(&c->asyncReqs, c, opacCloseAsyncCB);
	opacidmapClose(&c->asyncReqs);

	c->currSendReq = NULL;

	opabuffFree(&c->currResponse);
}
