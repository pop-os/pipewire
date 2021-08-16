/* Simple Plugin API
 *
 * Copyright © 2019 Wim Taymans <wim.taymans@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <spa/utils/defs.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/node/command.h>
#include <spa/node/event.h>

#include "pwtest.h"

PWTEST(node_io_abi_sizes)
{
#if defined(__x86_64__) && defined(__LP64__)
	pwtest_int_eq(sizeof(struct spa_io_buffers), 8U);
	pwtest_int_eq(sizeof(struct spa_io_memory), 16U);
	pwtest_int_eq(sizeof(struct spa_io_range), 16U);
	pwtest_int_eq(sizeof(struct spa_io_clock), 160U);
	pwtest_int_eq(sizeof(struct spa_io_latency), 24U);
	pwtest_int_eq(sizeof(struct spa_io_sequence), 16U);
	pwtest_int_eq(sizeof(struct spa_io_segment_bar), 64U);
	pwtest_int_eq(sizeof(struct spa_io_segment_video), 80U);
	pwtest_int_eq(sizeof(struct spa_io_segment), 184U);

	pwtest_int_eq(sizeof(struct spa_io_position), 1688U);
	pwtest_int_eq(sizeof(struct spa_io_rate_match), 48U);

	spa_assert(sizeof(struct spa_node_info) == 48);
	spa_assert(sizeof(struct spa_port_info) == 48);

	spa_assert(sizeof(struct spa_result_node_error) == 8);
	spa_assert(sizeof(struct spa_result_node_params) == 24);

	return PWTEST_PASS;
#else
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_buffers));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_memory));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_range));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_clock));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_latency));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_sequence));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_segment_bar));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_segment_video));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_segment));

	fprintf(stderr, "%zd\n", sizeof(struct spa_io_position));
	fprintf(stderr, "%zd\n", sizeof(struct spa_io_rate_match));

	fprintf(stderr, "%zd\n", sizeof(struct spa_node_info));
	fprintf(stderr, "%zd\n", sizeof(struct spa_port_info));

	fprintf(stderr, "%zd\n", sizeof(struct spa_result_node_error));
	fprintf(stderr, "%zd\n", sizeof(struct spa_result_node_params));

	return PWTEST_SKIP;
#endif

}

PWTEST(node_io_abi)
{
	/* io */
	pwtest_int_eq(SPA_IO_Invalid, 0);
	pwtest_int_eq(SPA_IO_Buffers, 1);
	pwtest_int_eq(SPA_IO_Range, 2);
	pwtest_int_eq(SPA_IO_Clock, 3);
	pwtest_int_eq(SPA_IO_Latency, 4);
	pwtest_int_eq(SPA_IO_Control, 5);
	pwtest_int_eq(SPA_IO_Notify, 6);
	pwtest_int_eq(SPA_IO_Position, 7);
	pwtest_int_eq(SPA_IO_RateMatch, 8);
	pwtest_int_eq(SPA_IO_Memory, 9);

	/* position state */
	pwtest_int_eq(SPA_IO_POSITION_STATE_STOPPED, 0);
	pwtest_int_eq(SPA_IO_POSITION_STATE_STARTING, 1);
	pwtest_int_eq(SPA_IO_POSITION_STATE_RUNNING, 2);

	return PWTEST_PASS;
}

PWTEST(node_command_abi)
{
	pwtest_int_eq(SPA_NODE_COMMAND_Suspend, 0);
	pwtest_int_eq(SPA_NODE_COMMAND_Pause, 1);
	pwtest_int_eq(SPA_NODE_COMMAND_Start, 2);
	pwtest_int_eq(SPA_NODE_COMMAND_Enable, 3);
	pwtest_int_eq(SPA_NODE_COMMAND_Disable, 4);
	pwtest_int_eq(SPA_NODE_COMMAND_Flush, 5);
	pwtest_int_eq(SPA_NODE_COMMAND_Drain, 6);
	pwtest_int_eq(SPA_NODE_COMMAND_Marker, 7);

	return PWTEST_PASS;
}

PWTEST(node_event_abi)
{
	pwtest_int_eq(SPA_NODE_EVENT_Error, 0);
	pwtest_int_eq(SPA_NODE_EVENT_Buffering, 1);
	pwtest_int_eq(SPA_NODE_EVENT_RequestRefresh, 2);

	return PWTEST_PASS;
}

#define TEST_FUNC(a,b,func, id)							\
do {										\
	off_t diff = SPA_PTRDIFF(&a.func, &a);					\
	a.func = b.func;							\
	pwtest_ptr_eq(diff, SPA_PTRDIFF(&b.func, &b));				\
	pwtest_bool_true(diff == 0 || (diff-1)/sizeof(void*) == id);		\
} while(0)

PWTEST(node_node_abi)
{
	struct spa_node_events e;
	struct spa_node_callbacks c;
	struct spa_node_methods m;
	struct {
		uint32_t version;
		void (*info) (void *data, const struct spa_node_info *info);
		void (*port_info) (void *data,
			enum spa_direction direction, uint32_t port,
			const struct spa_port_info *info);
		void (*result) (void *data, int seq, int res,
			uint32_t type, const void *result);
		void (*event) (void *data, const struct spa_event *event);
	} events = { SPA_VERSION_NODE_EVENTS, };
	struct {
		uint32_t version;
		int (*ready) (void *data, int state);
		int (*reuse_buffer) (void *data,
				uint32_t port_id,
				uint32_t buffer_id);
		int (*xrun) (void *data, uint64_t trigger, uint64_t delay,
				struct spa_pod *info);
	} callbacks = { SPA_VERSION_NODE_CALLBACKS, };
	struct {
		uint32_t version;
		int (*add_listener) (void *object,
			struct spa_hook *listener,
			const struct spa_node_events *events,
			void *data);
		int (*set_callbacks) (void *object,
			const struct spa_node_callbacks *callbacks,
			void *data);
		int (*sync) (void *object, int seq);
		int (*enum_params) (void *object, int seq,
			uint32_t id, uint32_t start, uint32_t max,
			const struct spa_pod *filter);
		int (*set_param) (void *object,
			uint32_t id, uint32_t flags,
			const struct spa_pod *param);
		int (*set_io) (void *object,
			uint32_t id, void *data, size_t size);
		int (*send_command) (void *object, const struct spa_command *command);
		int (*add_port) (void *object,
			enum spa_direction direction, uint32_t port_id,
			const struct spa_dict *props);
		int (*remove_port) (void *object,
			enum spa_direction direction, uint32_t port_id);
		int (*port_enum_params) (void *object, int seq,
			enum spa_direction direction, uint32_t port_id,
			uint32_t id, uint32_t start, uint32_t max,
			const struct spa_pod *filter);
		int (*port_set_param) (void *object,
			enum spa_direction direction,
			uint32_t port_id,
			uint32_t id, uint32_t flags,
			const struct spa_pod *param);
		int (*port_use_buffers) (void *object,
			enum spa_direction direction,
			uint32_t port_id,
			uint32_t flags,
			struct spa_buffer **buffers,
			uint32_t n_buffers);
		int (*port_set_io) (void *object,
			enum spa_direction direction,
			uint32_t port_id,
			uint32_t id,
			void *data, size_t size);
		int (*port_reuse_buffer) (void *object, uint32_t port_id, uint32_t buffer_id);
		int (*process) (void *object);
	} methods = { SPA_VERSION_NODE_METHODS, 0 };

	TEST_FUNC(e, events, version, 0);
	TEST_FUNC(e, events, info, SPA_NODE_EVENT_INFO);
	TEST_FUNC(e, events, port_info, SPA_NODE_EVENT_PORT_INFO);
	TEST_FUNC(e, events, result, SPA_NODE_EVENT_RESULT);
	TEST_FUNC(e, events, event, SPA_NODE_EVENT_EVENT);
	pwtest_int_eq(SPA_NODE_EVENT_NUM, 4);
	pwtest_int_eq(sizeof(e), sizeof(events));

	TEST_FUNC(c, callbacks, version, 0);
	TEST_FUNC(c, callbacks, ready, SPA_NODE_CALLBACK_READY);
	TEST_FUNC(c, callbacks, reuse_buffer, SPA_NODE_CALLBACK_REUSE_BUFFER);
	TEST_FUNC(c, callbacks, xrun, SPA_NODE_CALLBACK_XRUN);
	pwtest_int_eq(SPA_NODE_CALLBACK_NUM, 3);
	pwtest_int_eq(sizeof(c), sizeof(callbacks));

	TEST_FUNC(m, methods, version, 0);
	TEST_FUNC(m, methods, add_listener, SPA_NODE_METHOD_ADD_LISTENER);
	TEST_FUNC(m, methods, set_callbacks, SPA_NODE_METHOD_SET_CALLBACKS);
	TEST_FUNC(m, methods, sync, SPA_NODE_METHOD_SYNC);
	TEST_FUNC(m, methods, enum_params, SPA_NODE_METHOD_ENUM_PARAMS);
	TEST_FUNC(m, methods, set_param, SPA_NODE_METHOD_SET_PARAM);
	TEST_FUNC(m, methods, set_io, SPA_NODE_METHOD_SET_IO);
	TEST_FUNC(m, methods, send_command, SPA_NODE_METHOD_SEND_COMMAND);
	TEST_FUNC(m, methods, add_port, SPA_NODE_METHOD_ADD_PORT);
	TEST_FUNC(m, methods, remove_port, SPA_NODE_METHOD_REMOVE_PORT);
	TEST_FUNC(m, methods, port_enum_params, SPA_NODE_METHOD_PORT_ENUM_PARAMS);
	TEST_FUNC(m, methods, port_use_buffers, SPA_NODE_METHOD_PORT_USE_BUFFERS);
	TEST_FUNC(m, methods, port_set_io, SPA_NODE_METHOD_PORT_SET_IO);
	TEST_FUNC(m, methods, port_reuse_buffer, SPA_NODE_METHOD_PORT_REUSE_BUFFER);
	TEST_FUNC(m, methods, process, SPA_NODE_METHOD_PROCESS);
	pwtest_int_eq(SPA_NODE_METHOD_NUM, 15);
	pwtest_int_eq(sizeof(m), sizeof(methods));

	return PWTEST_PASS;
}

PWTEST_SUITE(spa_node)
{
	pwtest_add(node_io_abi_sizes, PWTEST_NOARG);
	pwtest_add(node_io_abi, PWTEST_NOARG);
	pwtest_add(node_command_abi, PWTEST_NOARG);
	pwtest_add(node_event_abi, PWTEST_NOARG);
	pwtest_add(node_node_abi, PWTEST_NOARG);

	return PWTEST_PASS;
}
