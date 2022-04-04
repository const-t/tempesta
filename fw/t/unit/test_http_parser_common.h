/**
 *		Tempesta FW
 *
 * Copyright (C) 2014 NatSys Lab. (info@natsys-lab.com).
 * Copyright (C) 2015-2022 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __TFW_HTTP_PARSER_COMMON_H__
#define __TFW_HTTP_PARSER_COMMON_H__

/*
 * Need to define DEBUG before first the inclusions of
 * lib/log.h and linux/printk.h.
 */
#if DBG_HTTP_PARSER > 0
#undef DEBUG
#define DEBUG DBG_HTTP_PARSER
#endif

#include <linux/types.h>
#include <asm/fpu/api.h>
#include <linux/vmalloc.h>

#include "test.h"
#include "helpers.h"
#include "fuzzer.h"

#ifdef EXPORT_SYMBOL
#undef EXPORT_SYMBOL
#endif
#define EXPORT_SYMBOL(...)

#include "http_parser.h"
#include "http_sess.h"
#include "str.h"
#include "ss_skb.h"
#include "msg.h"
#include "http_msg.h"

static const unsigned int CHUNK_SIZES[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128,
                                   256, 1500, 9216, 1024*1024
                                  /* to fit a message of 'any' size */
                                 };
static unsigned int chunk_size_index = 0;
#define CHUNK_SIZE_CNT ARRAY_SIZE(CHUNK_SIZES)

enum {
	CHUNK_OFF,
	CHUNK_ON
};

#define TOKEN_ALPHABET		"!#$%&'*+-.0123456789ABCDEFGHIJKLMNOPQ"	\
				"RSTUVWXYZ^_`abcdefghijklmnopqrstuvwxyz|~"
#define QETOKEN_ALPHABET	TOKEN_ALPHABET "\"="
#define OTHER_DELIMETERS	"(),/:;<=>?@[\\]{}"
#define OBS_TEXT		"\x80\x90\xC8\xAE\xFE\xFF"
#define ETAG_ALPHABET		OTHER_DELIMETERS TOKEN_ALPHABET OBS_TEXT
#define VCHAR_ALPHABET		"\x09 \"" OTHER_DELIMETERS 		\
				TOKEN_ALPHABET OBS_TEXT

#define EXPECT_BLOCK_DIGITS(head, tail, BLOCK_MACRO)		\
	BLOCK_MACRO(head tail);					\
	BLOCK_MACRO(head "  " tail);				\
	BLOCK_MACRO(head "5a" tail);				\
	BLOCK_MACRO(head "\"" tail);				\
	BLOCK_MACRO(head "=" tail);				\
	BLOCK_MACRO(head "-1" tail);				\
	BLOCK_MACRO(head "0.99" tail);				\
	BLOCK_MACRO(head "dummy" tail);				\
	BLOCK_MACRO(head "4294967296" tail);			\
	BLOCK_MACRO(head "9223372036854775807" tail);		\
	BLOCK_MACRO(head "9223372036854775808" tail);		\
	BLOCK_MACRO(head "18446744073709551615" tail);		\
	BLOCK_MACRO(head "18446744073709551616" tail)

#define EXPECT_BLOCK_SHORT(head, tail, BLOCK_MACRO)		\
	BLOCK_MACRO(head "65536" tail);				\
	BLOCK_MACRO(head "2147483647" tail);			\
	BLOCK_MACRO(head "2147483648" tail);			\
	BLOCK_MACRO(head "4294967295" tail)

/* For ETag and If-None-Match headers */
#define COMMON_ETAG_BLOCK(head, BLOCK_MACRO)			\
	BLOCK_MACRO(head "\"dummy");				\
	BLOCK_MACRO(head "dummy\"");				\
	BLOCK_MACRO(head "'dummy'");				\
	BLOCK_MACRO(head "W/ \"dummy\"");			\
	BLOCK_MACRO(head "w/\"dummy\"");			\
	BLOCK_MACRO(head "\"\x00\"");				\
	BLOCK_MACRO(head "\"\x0F\"");				\
	BLOCK_MACRO(head "\"\x7F\"");				\
	BLOCK_MACRO(head "\" \"");				\
	BLOCK_MACRO(head "\"\"\"")


static TfwHttpReq *req, *sample_req;
static TfwHttpResp *resp;
static TfwH2Conn conn;
static TfwStream stream;
static size_t hm_exp_len = 0;


typedef struct data_rec
{
	char *buf;
	size_t size;
} TfwDataRec;

typedef struct header_rec
{
	TfwDataRec name;
	TfwDataRec value;
} TfwHeaderRec;

typedef struct frame_rec
{
    unsigned int len;
    unsigned char *str;
    TfwFrameType subtype;
} TfwFrameRec;

static TfwFrameRec frames[2];
static unsigned int frames_cnt = 0;
static unsigned int frames_max_sz = 0;
static unsigned int frames_total_sz = 0;

static unsigned char frames_str_buf[3 * 1024] __attribute__((unused));
static unsigned char *frames_buf_ptr __attribute__((unused)) = NULL;
static unsigned int frames_buf_capacity __attribute__((unused)) = 0;

static
unsigned int __attribute__((unused))
tfw_h2_encode_data(TfwDataRec data)
{
	TfwHPackInt hpint;

	write_int(data.size, 0x7F, 0, &hpint);
	memcpy_fast(frames_buf_ptr, hpint.buf, hpint.sz);
	frames_buf_ptr += hpint.sz;
	memcpy_fast(frames_buf_ptr, data.buf, data.size);
	frames_buf_ptr += data.size;

	return hpint.sz + data.size;
}

static
unsigned int __attribute__((unused))
tfw_h2_encode_header(TfwHeaderRec header)
{
	static const int LIT_HDR_FLD_WO_IND  = 0x00;

	unsigned int header_sz = 0;

	*frames_buf_ptr = LIT_HDR_FLD_WO_IND;
	++frames_buf_ptr;
	++header_sz;

	header_sz += tfw_h2_encode_data(header.name);
	header_sz += tfw_h2_encode_data(header.value);

	return header_sz;
}

static
TfwDataRec __attribute__((unused))
__data_from_STR(char *data)
{
	TfwDataRec ret = {data, strlen(data)};
	return ret;
}

#define STR(data) \
	__data_from_STR(data)

static
TfwDataRec __attribute__((unused))
__data_from_RAW(char *data, size_t size)
{
	TfwDataRec ret = {data, size};
	return ret;
}

#define RAW(data) \
	__data_from_RAW(data, sizeof(data))

#define HEADERS_FRAME_BEGIN()							\
do {										\
	TfwHeaderRec temp;							\
	unsigned int frame_sz = 0;						\
	BUG_ON(frames_cnt >= ARRAY_SIZE(frames));				\
	frames[frames_cnt].subtype = HTTP2_HEADERS				\

#define HEADER(name_rec, value_rec)						\
do {										\
	temp.name = name_rec;							\
	temp.value = value_rec;							\
	frame_sz += tfw_h2_encode_header(temp);					\
	BUG_ON(frames_buf_ptr > frames_str_buf + frames_buf_capacity);		\
} while (0)

#define HEADERS_FRAME_END()							\
	BUG_ON(frames_cnt >= ARRAY_SIZE(frames));				\
	frames[frames_cnt].str = frames_buf_ptr - frame_sz;			\
	frames[frames_cnt].len = frame_sz;					\
	frames_total_sz += frame_sz;						\
	frames_max_sz = max(frames_max_sz, frame_sz);				\
	++frames_cnt;								\
} while (0)


#define DATA_FRAME_BEGIN()							\
do {										\
	unsigned int frame_sz = 0;						\
	BUG_ON(frames_cnt >= ARRAY_SIZE(frames));				\
	frames[frames_cnt].subtype = HTTP2_DATA					\

#define DATA(data_rec)								\
do {										\
	memcpy_fast(frames_buf_ptr, data_rec.buf, data_rec.size);		\
	frames_buf_ptr += data_rec.size;					\
	frame_sz += data_rec.size;						\
	BUG_ON(frames_buf_ptr > frames_str_buf + frames_buf_capacity);		\
} while (0)

#define DATA_FRAME_END()							\
	BUG_ON(frames_cnt >= ARRAY_SIZE(frames));				\
	frames[frames_cnt].str = frames_buf_ptr - frame_sz;			\
	frames[frames_cnt].len = frame_sz;					\
	frames_total_sz += frame_sz;						\
	frames_max_sz = max(frames_max_sz, frame_sz);				\
	++frames_cnt;								\
} while (0)


static int
split_and_parse_n(unsigned char *str, int type, size_t len, size_t chunk_size)
{
	size_t pos = 0;
	unsigned int parsed;
	int r = TFW_PASS;
	TfwHttpMsg *hm = (type == FUZZ_RESP)
			? (TfwHttpMsg *)resp
			: (TfwHttpMsg *)req;

	BUG_ON(type != FUZZ_REQ && type != FUZZ_REQ_H2 && type != FUZZ_RESP);

	TEST_DBG3("%s: len=%zu, chunk_size=%zu\n", __func__, len, chunk_size);

	while (pos < len) {
		if (chunk_size >= len - pos)
			/* At the last chunk */
			chunk_size = len - pos;
		TEST_DBG3("%s: len=%zu pos=%zu\n",  __func__, len, pos);
		if (type == FUZZ_REQ)
			r = tfw_http_parse_req(req, str + pos, chunk_size, &parsed);
		else if (type == FUZZ_REQ_H2)
			r = tfw_h2_parse_req(req, str + pos, chunk_size, &parsed);
		else
			r = tfw_http_parse_resp(resp, str + pos, chunk_size, &parsed);

		pos += chunk_size;
		hm->msg.len += parsed;

		BUILD_BUG_ON((int)TFW_POSTPONE != (int)T_POSTPONE);
		if (r != TFW_POSTPONE)
			return r;
	}
	BUG_ON(pos != len);

	return r;
}

/**
 * Response must be paired with request to be parsed correctly. Update sample
 * request for further response parsing.
 */
static int __attribute__((unused))
set_sample_req(unsigned char *str)
{
	size_t len = strlen(str);
	unsigned int parsed;

	if (sample_req)
		test_req_free(sample_req);

	sample_req = test_req_alloc(len);

	return tfw_http_parse_req(sample_req, str, len, &parsed);
}

static void
test_case_parse_prepare_http(char *str, size_t sz_diff)
{
	size_t str_len = strlen(str);

	chunk_size_index = 0;

	frames_max_sz = str_len;
	frames_cnt = 1;

	frames[0].str = str;
	frames[0].len = str_len;

	frames_total_sz = str_len;

	hm_exp_len = str_len - sz_diff;
}

static void
test_case_parse_prepare_h2(void)
{
	tfw_h2_context_init(&conn.h2);
	conn.h2.hdr.type = HTTP2_HEADERS;
	stream.state = HTTP2_STREAM_REM_HALF_CLOSED;

	chunk_size_index = 0;
	hm_exp_len = frames_total_sz;
}



/**
 * The function is designed to be called in a loop, e.g.
 *   while(!do_split_and_parse(str, len, type, chunk_mode)) { ... }
 *
 * type may be FUZZ_REQ or FUZZ_REQ_H2 or FUZZ_RESP.
 *
 * On each iteration it splits the @str into fragments and pushes
 * them to the HTTP parser.
 *
 * That is done because:
 *  - HTTP pipelining: the feature implies that such a "split" may occur at
 *    any position of the input string. THe HTTP parser should be able to handle
 *    that, and we would like to test it.
 *  - Code coverage: the parser contains some optimizations for non-fragmented
 *    data, so we need to generate all possible fragments to test both "fast
 *    path" and "slow path" execution.
 *
 * The function is stateful:
 *  - It puts the parsed request or response to the global variable
 *  @req or @resp (on each call, depending on the message type).
 *  - It maintains the internal state between calls.
 *
 * Return value:
 *  == 0 - OK: current step of the loop is done without errors, proceed.
 *  <  0 - Error: the parsing is failed.
 *  >  0 - EOF: all possible fragments are parsed, terminate the loop.
 */

static int
do_split_and_parse(int type, int chunk_mode)
{
	int r;
	unsigned int chunk_size;
	unsigned int frame_index;

	if (chunk_size_index == CHUNK_SIZE_CNT)
		/*
		 * Return any positive value to indicate that
		 * all defined chunk sizes were tested and
		 * no more iterations needed.
		 */
		return 1;

	if (type == FUZZ_REQ) {
		if (req)
			test_req_free(req);

		req = test_req_alloc(frames_max_sz);
	}
	else if (type == FUZZ_REQ_H2) {
		if (req)
			test_req_free(req);

		req = test_req_alloc(frames_max_sz);
		conn.h2.hpack.state = 0;
		req->conn = (TfwConn*)&conn;
		req->pit.parsed_hdr = &stream.parser.hdr;
		req->stream = &stream;
		tfw_http_init_parser_req(req);
		stream.msg = (TfwMsg*)req;
		__set_bit(TFW_HTTP_B_H2, req->flags);
	}
	else if (type == FUZZ_RESP) {
		if (resp)
			test_resp_free(resp);

		resp = test_resp_alloc(frames_max_sz);
		tfw_http_msg_pair(resp, sample_req);
	}
	else {
		BUG();
	}

	chunk_size = chunk_mode == CHUNK_OFF
			? frames_max_sz
			: CHUNK_SIZES[chunk_size_index];

	TEST_DBG3("%s: chunk_mode=%d, chunk_size_index=%u, chunk_size=%u\n",
		    __func__, chunk_mode, chunk_size_index, chunk_size);

	for (frame_index = 0; frame_index < frames_cnt; ++frame_index)
	{
		TfwFrameRec frame = frames[frame_index];

		if (type == FUZZ_REQ_H2) {
			TfwH2Ctx *ctx = tfw_h2_context(req->conn);
			ctx->hdr.type = frame.subtype;
			ctx->plen = frame.len;
		}

		if (type == FUZZ_REQ_H2
		    && frame.subtype == HTTP2_DATA
		    && !frame.len)
		{
			r = TFW_POSTPONE;
		}
		else
		{
			r = split_and_parse_n(frame.str, type, frame.len, chunk_size);
		}

		if (r != TFW_POSTPONE)
			break;

		if (type == FUZZ_REQ_H2 && frame.subtype == HTTP2_HEADERS) {
			if (!tfw_http_parse_check_bodyless_meth(req)) {
				__set_bit(TFW_HTTP_B_HEADERS_PARSED, req->flags);
			}
			else
			{
				r = TFW_BLOCK;
				break;
			}
		}
	}

	if (type == FUZZ_REQ_H2 && r == TFW_POSTPONE) {
		r = tfw_h2_parse_req_finish(req);
	}

	if (chunk_mode == CHUNK_OFF || CHUNK_SIZES[chunk_size_index] >= frames_max_sz)
		/*
		 * Stop splitting message into pieces bigger than
		 * the message itself.
		 */
		chunk_size_index = CHUNK_SIZE_CNT;
	else
		/* Try next size, if any. on next interation */
		chunk_size_index++;

	return r;
}

/**
 * To validate message parsing we provide text string which describes
 * HTTP message from start to end. If there any unused bytes after
 * message is successfully parsed, then parsing was incorrect.
 */
static
int __attribute__((unused))
validate_data_fully_parsed(int type)
{
	TfwHttpMsg *hm = (type == FUZZ_REQ || type == FUZZ_REQ_H2)
			? (TfwHttpMsg *)req
			: (TfwHttpMsg *)resp;

	EXPECT_EQ(hm->msg.len, hm_exp_len);
	return hm->msg.len == hm_exp_len;
}

#define TRY_PARSE_EXPECT_PASS(type, chunk_mode)				\
({									\
	int _err = do_split_and_parse(type, chunk_mode);		\
	if (_err == TFW_BLOCK || _err == TFW_POSTPONE			\
	    || !validate_data_fully_parsed(type))   			\
		TEST_FAIL("can't parse %s (code=%d)\n",			\
			  (type == FUZZ_REQ	    			\
			   || type == FUZZ_REQ_H2			\
			   ? "request" : "response"),			\
			  _err);					\
	__fpu_schedule();						\
	_err == TFW_PASS;						\
})

#define TRY_PARSE_EXPECT_BLOCK(type, chunk_mode)			\
({									\
	int _err = do_split_and_parse(type, chunk_mode);		\
	if (_err == TFW_PASS)						\
		TEST_FAIL("%s is not blocked as expected\n",		\
			  (type == FUZZ_REQ				\
			   || type == FUZZ_REQ_H2			\
			   ? "request" : "response"));			\
	__fpu_schedule();						\
	_err == TFW_BLOCK || _err == TFW_POSTPONE;			\
})

#define __FOR_REQ(str, sz_diff, type, chunk_mode)			\
	TEST_LOG("=== request: [%s]\n", str);				\
	type == FUZZ_REQ_H2 ?						\
		test_case_parse_prepare_h2() :				\
		test_case_parse_prepare_http(str, sz_diff);		\
	while (TRY_PARSE_EXPECT_PASS(type, chunk_mode))

#define H2_BUILDER_SET_BUF(h2_builder_buf)				\
	BUG_ON(frames_buf_ptr);						\
	frames_buf_ptr = h2_builder_buf;				\
	frames_buf_capacity = sizeof(h2_builder_buf);			\
	bzero_fast(frames_buf_ptr, frames_buf_capacity)

#define H2_BUILDER_RESET_BUF()						\
	BUG_ON(!frames_buf_ptr);					\
	frames_buf_ptr = NULL;						\
	frames_buf_capacity = 0

#define H2_BUILDER_INIT()						\
	frames_cnt = 0;							\
	frames_total_sz = 0;						\
	frames_max_sz = 0

#define H2_BUILDER_GET_BUF_SIZE() \
	({frames_total_sz;})

#define FOR_REQ(str)							\
	__FOR_REQ(str, 0, FUZZ_REQ, CHUNK_ON)
#define FOR_REQ_H2(H2_BUILDER_BLOCK)					\
	H2_BUILDER_INIT();						\
	H2_BUILDER_SET_BUF(frames_str_buf);				\
	H2_BUILDER_BLOCK;						\
	H2_BUILDER_RESET_BUF();						\
	__FOR_REQ(							\
	    "HTTP/2 request preview is not available now...",		\
	     0, FUZZ_REQ_H2, CHUNK_ON)
#define FOR_REQ_H2_CHUNK_OFF(str)					\
	__FOR_REQ(str, 0, FUZZ_REQ_H2, CHUNK_OFF)

#define __EXPECT_BLOCK_REQ(str, type, chunk_mode)			\
do {									\
	TEST_LOG("=== request: [%s]\n", str);				\
	type == FUZZ_REQ_H2 ?						\
		test_case_parse_prepare_h2() :				\
		test_case_parse_prepare_http(str, 0);			\
	while (TRY_PARSE_EXPECT_BLOCK(type, chunk_mode));		\
} while (0)

#define EXPECT_BLOCK_REQ(str)						\
	__EXPECT_BLOCK_REQ(str, FUZZ_REQ, CHUNK_ON)
#define EXPECT_BLOCK_REQ_H2(H2_BUILDER_BLOCK)				\
	H2_BUILDER_INIT();						\
	H2_BUILDER_SET_BUF(frames_str_buf);				\
	H2_BUILDER_BLOCK;						\
	H2_BUILDER_RESET_BUF();						\
	__EXPECT_BLOCK_REQ(						\
	    "HTTP/2 request preview is not available now...",		\
	    FUZZ_REQ_H2, CHUNK_ON)
#define EXPECT_BLOCK_REQ_H2_CHUNK_OFF(str)				\
	__EXPECT_BLOCK_REQ(str, FUZZ_REQ_H2, CHUNK_OFF)

#define __FOR_RESP(str, sz_diff, chunk_mode)				\
	TEST_LOG("=== response: [%s]\n", str);				\
	test_case_parse_prepare_http(str, sz_diff);			\
	while (TRY_PARSE_EXPECT_PASS(FUZZ_RESP, chunk_mode))

#define FOR_RESP(str)	__FOR_RESP(str, 0, CHUNK_ON)

#define EXPECT_BLOCK_RESP(str)						\
do {									\
	TEST_LOG("=== response: [%s]\n", str);				\
	test_case_parse_prepare_http(str, 0);				\
	while (TRY_PARSE_EXPECT_BLOCK(FUZZ_RESP, CHUNK_ON));		\
} while (0)

#define EXPECT_TFWSTR_EQ(tfw_str, cstr)					\
	EXPECT_TRUE(tfw_str_eq_cstr(tfw_str, cstr, strlen(cstr), 0))

/*
 * Test that the parsed string was split to the right amount of chunks and all
 * the chunks has the same flags.
 */
static void __attribute__((unused))
test_string_split(const TfwStr *expected, const TfwStr *parsed)
{
	TfwStr *end_p, *end_e, *c_p, *c_e;

	BUG_ON(TFW_STR_PLAIN(expected));
	EXPECT_FALSE(TFW_STR_PLAIN(parsed));
	if (TFW_STR_PLAIN(parsed))
		return;

	EXPECT_GE(parsed->nchunks, expected->nchunks);
	EXPECT_EQ(parsed->len, expected->len);
	if (parsed->len != expected->len)
		return;

	c_p = parsed->chunks;
	end_p = c_p + parsed->nchunks;
	c_e = expected->chunks;
	end_e = c_e + expected->nchunks;

	while (c_e < end_e) {
		unsigned short flags = c_e->flags;
		TfwStr e_part = { .chunks = c_e }, p_part = { .chunks = c_p };

		while ((c_e < end_e) && (c_e->flags == flags)) {
			e_part.nchunks++;
			e_part.len += c_e->len;
			c_e++;
		}
		while ((c_p < end_p) && (c_p->flags == flags)) {
			p_part.nchunks++;
			p_part.len += c_p->len;
			c_p++;
		}
		EXPECT_EQ(p_part.len, e_part.len);
		if (p_part.len != e_part.len)
			return;
		EXPECT_OK(tfw_strcmp(&e_part, &p_part));
	}
	EXPECT_EQ(c_p, end_p);
	EXPECT_EQ(c_e, end_e);
}

static inline int
number_to_strip(TfwHttpReq *req)
{
	return
		!!test_bit(TFW_HTTP_B_NEED_STRIP_LEADING_CR, req->flags) +
		!!test_bit(TFW_HTTP_B_NEED_STRIP_LEADING_LF, req->flags);
}

static
TfwStr get_next_str_val(TfwStr *str)
{
	TfwStr v, *c, *end;
	unsigned int nchunks = 0;

	v = *str = tfw_str_next_str_val(str);
	TFW_STR_FOR_EACH_CHUNK(c, &v, end) {
		if (!(c->flags & TFW_STR_VALUE))
			break;
		nchunks++;
	}
	v.nchunks = nchunks;

	return v;
}

#endif /* __TFW_HTTP_PARSER_COMMON_H__ */
