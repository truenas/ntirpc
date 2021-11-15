/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * rpc_prot.c
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * This set of routines implements the rpc message definition,
 * its serializer and some common rpc utility routines.
 * The routines are meant for various implementations of rpc -
 * they are NOT for the rpc client or rpc service implementations!
 * Because authentication stuff is easy and is part of rpc, the opaque
 * routines are also in this program.
 */

#include "config.h"
#include <sys/param.h>

#include <assert.h>

#include <rpc/rpc.h>
#include <rpc/xdr_inline.h>
#include <rpc/auth_inline.h>

static void accepted(enum accept_stat, struct rpc_err *);
static void rejected(enum reject_stat, struct rpc_err *);

/* * * * * * * * * * * * * * XDR Authentication * * * * * * * * * * * */

extern struct opaque_auth _null_auth;

/*
 * XDR a DES block
 */
bool
xdr_des_block(XDR *xdrs, des_block *blkp)
{
	assert(xdrs != NULL);
	assert(blkp != NULL);

	return (xdr_opaque(xdrs, (char *)blkp, sizeof(des_block)));
}

/* * * * * * * * * * * * * * XDR RPC MESSAGE * * * * * * * * * * * * * * * */

/*
 * XDR the MSG_ACCEPTED part of a reply message union
 */
bool
xdr_naccepted_reply(XDR *xdrs, struct accepted_reply *ar)
{
	assert(xdrs != NULL);
	assert(ar != NULL);

	/* personalized union, rather than calling xdr_union */
	if (!xdr_opaque_auth(xdrs, &(ar->ar_verf)))
		return (false);
	if (!inline_xdr_enum(xdrs, (enum_t *) &(ar->ar_stat)))
		return (false);
	switch (ar->ar_stat) {

	case SUCCESS:
		return ((*(ar->ar_results.proc)) (xdrs, ar->ar_results.where));

	case PROG_MISMATCH:
		if (!inline_xdr_u_int32_t(xdrs, &(ar->ar_vers.low)))
			return (false);
		return (inline_xdr_u_int32_t(xdrs, &(ar->ar_vers.high)));

	case GARBAGE_ARGS:
	case SYSTEM_ERR:
	case PROC_UNAVAIL:
	case PROG_UNAVAIL:
		break;
	}
	return (true);		/* true => open ended set of problems */
}

/*
 * XDR the MSG_DENIED part of a reply message union
 */
bool
xdr_nrejected_reply(XDR *xdrs, struct rejected_reply *rr)
{
	assert(xdrs != NULL);
	assert(rr != NULL);

	/* personalized union, rather than calling xdr_union */
	if (!inline_xdr_enum(xdrs, (enum_t *) &(rr->rj_stat)))
		return (false);
	switch (rr->rj_stat) {

	case RPC_MISMATCH:
		if (!inline_xdr_u_int32_t(xdrs, &(rr->rj_vers.low)))
			return (false);
		return (inline_xdr_u_int32_t(xdrs, &(rr->rj_vers.high)));

	case AUTH_ERROR:
		return (inline_xdr_enum(xdrs, (enum_t *) &(rr->rj_why)));
	}
	/* NOTREACHED */
	assert(0);
	return (false);
}

static const struct xdr_discrim reply_dscrm[3] = {
	{(int)MSG_ACCEPTED, (xdrproc_t) xdr_naccepted_reply},
	{(int)MSG_DENIED, (xdrproc_t) xdr_nrejected_reply},
	{__dontcare__, NULL_xdrproc_t}
};

/*
 * XDR a reply message
 */
bool
xdr_nreplymsg(XDR *xdrs, struct rpc_msg *rmsg)
{
	assert(xdrs != NULL);
	assert(rmsg != NULL);

	if (inline_xdr_u_int32_t(xdrs, &(rmsg->rm_xid))
	    && inline_xdr_enum(xdrs, (enum_t *) &(rmsg->rm_direction))
	    && (rmsg->rm_direction == REPLY))
		return (inline_xdr_union(xdrs,
					 (enum_t *)&(rmsg->rm_reply.rp_stat),
					 (void *)&(rmsg->rm_reply.ru),
					 reply_dscrm, NULL_xdrproc_t));
	return (false);
}

/*
 * Serializes the "static part" of a call message header.
 * The fields include: rm_xid, rm_direction, rpcvers, prog, and vers.
 * The rm_xid is not really static, but the user can easily munge on the fly.
 */
bool
xdr_ncallhdr(XDR *xdrs, struct rpc_msg *cmsg)
{
	assert(xdrs != NULL);
	assert(cmsg != NULL);

	cmsg->rm_direction = CALL;
	cmsg->rm_call.cb_rpcvers = RPC_MSG_VERSION;
	if ((xdrs->x_op == XDR_ENCODE)
	    && inline_xdr_u_int32_t(xdrs, &(cmsg->rm_xid))
	    && inline_xdr_enum(xdrs, (enum_t *) &(cmsg->rm_direction))
	    && inline_xdr_u_int32_t(xdrs, &(cmsg->rm_call.cb_rpcvers))
	    && inline_xdr_u_int32_t(xdrs, &(cmsg->cb_prog)))
		return (inline_xdr_u_int32_t(xdrs, &(cmsg->cb_vers)));
	return (false);
}

/* ************************** Client utility routine ************* */

static void
accepted(enum accept_stat acpt_stat, struct rpc_err *error)
{
	assert(error != NULL);

	switch (acpt_stat) {

	case PROG_UNAVAIL:
		error->re_status = RPC_PROGUNAVAIL;
		return;

	case PROG_MISMATCH:
		error->re_status = RPC_PROGVERSMISMATCH;
		return;

	case PROC_UNAVAIL:
		error->re_status = RPC_PROCUNAVAIL;
		return;

	case GARBAGE_ARGS:
		error->re_status = RPC_CANTDECODEARGS;
		return;

	case SYSTEM_ERR:
		error->re_status = RPC_SYSTEMERROR;
		return;

	case SUCCESS:
		error->re_status = RPC_SUCCESS;
		return;
	}
	/* NOTREACHED */
	/* something's wrong, but we don't know what ... */
	error->re_status = RPC_FAILED;
	error->re_lb.s1 = (int32_t) MSG_ACCEPTED;
	error->re_lb.s2 = (int32_t) acpt_stat;
}

static void
rejected(enum reject_stat rjct_stat, struct rpc_err *error)
{
	assert(error != NULL);

	switch (rjct_stat) {
	case RPC_MISMATCH:
		error->re_status = RPC_VERSMISMATCH;
		return;

	case AUTH_ERROR:
		error->re_status = RPC_AUTHERROR;
		return;
	}
	/* something's wrong, but we don't know what ... */
	/* NOTREACHED */
	error->re_status = RPC_FAILED;
	error->re_lb.s1 = (int32_t) MSG_DENIED;
	error->re_lb.s2 = (int32_t) rjct_stat;
}

/*
 * given a reply message, fills in the error
 */
void
_seterr_reply(struct rpc_msg *msg, struct rpc_err *error)
{
	assert(msg != NULL);
	assert(error != NULL);

	/* optimized for normal, SUCCESSful case */
	switch (msg->rm_reply.rp_stat) {

	case MSG_ACCEPTED:
		if (msg->RPCM_ack.ar_stat == SUCCESS) {
			error->re_status = RPC_SUCCESS;
			return;
		}
		accepted(msg->RPCM_ack.ar_stat, error);
		break;

	case MSG_DENIED:
		rejected(msg->RPCM_rej.rj_stat, error);
		break;

	default:
		error->re_status = RPC_FAILED;
		error->re_lb.s1 = (int32_t) (msg->rm_reply.rp_stat);
		break;
	}
	switch (error->re_status) {

	case RPC_VERSMISMATCH:
		error->re_vers.low = msg->RPCM_rej.rj_vers.low;
		error->re_vers.high = msg->RPCM_rej.rj_vers.high;
		break;

	case RPC_AUTHERROR:
		error->re_why = msg->RPCM_rej.rj_why;
		break;

	case RPC_PROGVERSMISMATCH:
		error->re_vers.low = msg->RPCM_ack.ar_vers.low;
		error->re_vers.high = msg->RPCM_ack.ar_vers.high;
		break;

	case RPC_FAILED:
	case RPC_SUCCESS:
	case RPC_PROGNOTREGISTERED:
	case RPC_PMAPFAILURE:
	case RPC_UNKNOWNPROTO:
	case RPC_UNKNOWNHOST:
	case RPC_SYSTEMERROR:
	case RPC_CANTDECODEARGS:
	case RPC_PROCUNAVAIL:
	case RPC_PROGUNAVAIL:
	case RPC_TIMEDOUT:
	case RPC_CANTRECV:
	case RPC_CANTSEND:
	case RPC_CANTDECODERES:
	case RPC_CANTENCODEARGS:
	default:
		break;
	}
}
