/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPAC_H_
#define OPAC_H_

#include "opabuff.h"
#include "opacidmap.h"
#include "opapp.h"
#include "opaqueue.h"


typedef struct {
	opaqueueItem qi;
	opabuff rrbuff;      // stores request before/during serialization; then response when received
	const uint8_t* pos;  // when request is being serialized, stores write pos; when response received, stores pos of result or error
	unsigned char flags;
} opacReq;

typedef struct {
	opacReq rbase;
	opacidmapItem idinfo;
} opacReqAsync;

typedef struct {
	int err;
	char closed;
	const struct opacFuncs_s* cbs;

	uint64_t currId;
	opaqueue reqsToSend;  // requests that are waiting to be sent
	opacReq* currSendReq; // partially sent request

	opaqueue mainReqs;    // all non-async requests that have been sent and are waiting for a response from server
	opacidmap asyncReqs;  // all async requests that have been sent and are waiting for a response from server

	opapp pp;
	opabuff currResponse;
} opac;

typedef enum {
	OPAC_RER_INVREQ = 1,  // invalid request
	OPAC_RER_IDEXISTS,    // async id already exists (internal error, should not happen)
	OPAC_RER_ERR,         // an error occurred (ie, out of memory). see errCode
	OPAC_RER_CLOSED       // client is closed or encountered an error and cannot continue
} opacReqErrReason;

typedef struct opacFuncs_s {
	// try to read len bytes. return number of bytes read into buff. return 0 to indicate EWOULDBLOCK/CLOSED/error
	size_t (*read) (opac* c, void* buff, size_t len);
	// try to write len bytes. return number of bytes written from buff. return 0 to indicate EWOULDBLOCK/CLOSED/error
	size_t (*write)(opac* c, const void* buff, size_t len);

	// function that is called when client encounters an error and cannot proceed (ie, out of memory or parse error)
	void (*clientErr)(opac* c, int errCode);

	// function that is called when the specified request has been fully written. can be null
	// note: if not null, then function is responsible for freeing the request's buffer with opacReqFreeRequest()
	void (*onSent)(opac* c, opacReq* r);

	// function that is called when a response is received. responsible for freeing response buffer with opacReqFreeResponse()
	void (*onResponse)(opac* c, opacReq* r);

	// function that is called when a request is not sent or is sent and expects a response but will never receive one. can be null
	// note: if not null then function is responsible for checking whether request was sent and - if not sent - freeing the
	//  request's buffer with opacReqFreeRequest()
	void (*reqErr)(opac* c, opacReq* r, opacReqErrReason reason, int errCode);

	// function that is called when client receives a message with an asyncid that was never sent by client; null for default handling
	// note: if not null, then function is responsible for freeing the buffer with opabuffFree()
	void (*unknownAsyncId)(opac* c, opabuff rawData);
} opacFuncs;

typedef struct {
	int32_t code;
	const void* msg;
	size_t msgLen;
	const void* data;
} opacRpcError;

typedef struct {
	const char* version;
	char threadSupport;
	const char* bigIntLib;
} opacBuildInfo;



void opacReqInit(opacReq* r);
void opacReqSetRequestBuff(opacReq* r, opabuff b);
void opacReqFreeRequest(opacReq* r);
void opacReqFreeResponse(opacReq* r);

/**
 * Determine whether request has been sent
 * @param r Request/response object to check
 * @return 0 if response not yet written; else non-zero
 */
int opacReqIsSent(const opacReq* r);

/**
 * Determine whether a response has been received for request
 * @param r Request/response object to check
 * @return 0 if response not yet received; else non-zero
 */
int opacReqResponseRecvd(const opacReq* r);

/**
 * Determine whether the response received was an error
 * @param r Request/response object to check
 * @return -1 if response not yet received; 0 if response is not err; else value greater than zero
 */
int opacReqResponseIsErr(const opacReq* r);

/**
 * Get a pointer to the serialized response object. If response was an error (determined from
 * calling opacReqResponseIsErr() function) then this will return a pointer to the serialized
 * error object (otherwise a pointer to the serialized result). Returns NULL if a response was
 * not yet received.
 */
const uint8_t* opacReqGetResponse(const opacReq* r);

/**
 * If the response was an error then parse the serialized value into an opacRpcError structure.
 * If response was not an error then returns err code.
 * @param r request/response object
 * @param errobj The error structure to populate
 * @return error code if response was not received, response was not an error, or an error
 * occurs when trying to load the serialized error
 */
int opacReqLoadErrObj(const opacReq* r, opacRpcError* errobj);


/**
 * Get build info for the opac library
 */
const opacBuildInfo* opacGetBuildInfo(void);

/**
 * Initialize a client that will be used by only 1 thread at a time
 * @param c Client
 * @param funcs Callback functions
 */
void opacInit(opac* c, const opacFuncs* funcs);

#ifndef OPA_NOTHREADS
/**
 * Initialize a client that can be used by multiple threads
 * @param c Client
 * @param funcs Callback functions
 */
void opacInitMT(opac* c, const opacFuncs* funcs);
#endif

/**
 * Return zero if client is closed or encountered an error; else non-zero
 * @param c Client
 */
int opacIsOpen(opac* c);

/**
 * Queue a request that will be sent eventually when opacSendRequests() is called.
 * @param c Client
 * @param r Request to send
 */
void opacQueueRequest(opac* c, opacReq* r);

/**
 * Queue an async request that will receive a response from server out of order. Request
 * will be sent eventually when opacSendRequests() is called.
 * Note: request buffer will be modified (to add async-id to end of request)
 * @param c Client
 * @param r Request to send
 * @param persistent 0 if server will only respond once; else non-zero
 */
void opacQueueAsyncRequest(opac* c, opacReqAsync* r, int persistent);

/**
 * Queue a request that will not receive a response from server. Request will be sent
 * eventually when opacSendRequests() is called.
 * Note: request buffer will be modified (to add null async-id to end of request)
 * @param c Client
 * @param r Request to send
 */
void opacQueueNoResponseRequest(opac* c, opacReq* r);

/**
 * Unregister the specified persistent async request
 * @param c Client
 * @param r Request
 * @return non-zero if persistent request id was found and removed; else return zero
 */
int opacRemovePersistent(opac* c, opacReqAsync* r);

/**
 * Try to send all queued requests. Stops when write callback returns 0.
 * @param c Client
 */
void opacSendRequests(opac* c);

/**
 * Try to recv and parse responses from server. Stops when read callback returns 0.
 * @param c Client
 */
void opacParseResponses(opac* c);

/**
 * Close client. Any remaining requests that have not been fully sent or have been
 * sent and expect a response will be passed to the client's reqErr() callback (if
 * it is not null). Must be called after all other functions are done running (ie,
 * opacSendRequests, opacParseResponses, opacQueueRequest, etc).
 * @param c Client
 */
void opacClose(opac* c);


#endif
