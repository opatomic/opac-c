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

void opacReqAsyncInit(opacReqAsync* r, opacid id) {
	opacReqInit(&r->rbase);
	r->idinfo.id = id;
	r->rbase.flags |= OPAC_F_ISASYNC;
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
	if (*buff == OPADEF_ARRAY_END) {
		return OPA_ERR_PARSE;
	}
	const uint8_t* errObj = NULL;
	const uint8_t* asyncId = buff;
	buff += opasolen(buff);
	if (*buff == OPADEF_ARRAY_END) {
		return OPA_ERR_PARSE;
	}
	const uint8_t* result = buff;
	buff += opasolen(buff);
	if (*buff != OPADEF_ARRAY_END) {
		errObj = buff;
		buff += opasolen(buff);
		if (*buff != OPADEF_ARRAY_END) {
			return OPA_ERR_PARSE;
		}
	}

	if (errObj != NULL && *errObj != OPADEF_NULL) {
		result = NULL;
	} else {
		errObj = NULL;
	}
	if (*asyncId == OPADEF_FALSE) {
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
	if (*asyncId != OPADEF_NULL) {
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

opacid opacGetAsyncId(opac* c, int persistent) {
	// TODO: detect/prevent overflow of id?
	uint64_t id = ATOMIC_INC64(&c->currId);
	return persistent ? 0 - id : id;
}

int opacRemovePersistent(opac* c, opacReqAsync* r) {
	opacidmapItem* i = opacidmapGet(&c->asyncReqs, r->idinfo.id, 1);
	return i != NULL;
}

void opacQueueRequest(opac* c, opacReq* r) {
	OASSERT((r->flags & (OPAC_F_QUEUEDFORSEND | OPAC_F_SENT | OPAC_F_RESPONSERECVD | OPAC_F_RESULTISERR)) == 0);

	if (opabuffGetLen(&r->rrbuff) > 2) {
		const uint8_t* pos = opabuffGetPos(&r->rrbuff, 0);
		if (*pos != OPADEF_ARRAY_START) {
			goto InvalidReq;
		}
		++pos;
		if (*pos == OPADEF_NULL) {
			if (r->flags & (OPAC_F_ISASYNC | OPAC_F_NORESPONSE)) {
				goto InvalidReq;
			}
		} else if (*pos == OPADEF_FALSE) {
			if (r->flags & OPAC_F_ISASYNC) {
				goto InvalidReq;
			}
			r->flags |= OPAC_F_NORESPONSE;
		} else if (*pos == OPADEF_POSVARINT || *pos == OPADEF_NEGVARINT) {
			if ((r->flags & OPAC_F_ISASYNC) == 0) {
				goto InvalidReq;
			}
		}
	} else {
		goto InvalidReq;
	}

	if (c->err || c->closed) {
		opacHandleReqErr(c, r, OPAC_RER_CLOSED, 0);
		return;
	}

	r->pos = opabuffGetPos(&r->rrbuff, 0);
	r->flags |= OPAC_F_QUEUEDFORSEND;
	opaqueuePush(&c->reqsToSend, &r->qi);
	return;

	InvalidReq:
	opacHandleReqErr(c, r, OPAC_RER_INVREQ, 0);
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
		opacHandleReqErr(c, c->currSendReq, OPAC_RER_CLOSED, 0);
	}

	while (1) {
		opaqueueItem* qi = opaqueuePoll(&c->reqsToSend);
		if (qi == NULL) {
			break;
		}
		opacHandleReqErr(c, list_entry(qi, opacReq, qi), OPAC_RER_CLOSED, 0);
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
#ifndef OPA_NOTHREADS
	opaqueueClose(&c->mainReqs);
	opaqueueClose(&c->reqsToSend);
#endif

	c->currSendReq = NULL;

	opabuffFree(&c->currResponse);
}
