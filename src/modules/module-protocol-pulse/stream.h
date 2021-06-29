/* PipeWire
 *
 * Copyright © 2020 Wim Taymans
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

#ifndef PULSER_SERVER_STREAM_H
#define PULSER_SERVER_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include <spa/utils/hook.h>
#include <spa/utils/ringbuffer.h>
#include <pipewire/pipewire.h>

#include "format.h"
#include "volume.h"

struct impl;
struct client;
struct spa_io_rate_match;

struct buffer_attr {
	uint32_t maxlength;
	uint32_t tlength;
	uint32_t prebuf;
	uint32_t minreq;
	uint32_t fragsize;
};

struct stream {
	uint32_t create_tag;
	uint32_t channel;	/* index in map */
	uint32_t id;		/* id of global */

	struct impl *impl;
	struct client *client;
#define STREAM_TYPE_RECORD	0
#define STREAM_TYPE_PLAYBACK	1
#define STREAM_TYPE_UPLOAD	2
	uint32_t type;
	enum pw_direction direction;

	struct pw_properties *props;

	struct pw_stream *stream;
	struct spa_hook stream_listener;

	struct spa_io_rate_match *rate_match;
	struct spa_ringbuffer ring;
	void *buffer;

	int64_t read_index;
	int64_t write_index;
	uint64_t underrun_for;
	uint64_t playing_for;
	uint64_t ticks_base;
	uint64_t timestamp;
	int64_t delay;

	uint32_t missing;
	uint32_t requested;

	struct sample_spec ss;
	struct channel_map map;
	struct buffer_attr attr;
	uint32_t frame_size;
	uint32_t rate;

	struct volume volume;
	bool muted;

	uint32_t drain_tag;
	unsigned int corked:1;
	unsigned int draining:1;
	unsigned int volume_set:1;
	unsigned int muted_set:1;
	unsigned int early_requests:1;
	unsigned int adjust_latency:1;
	unsigned int is_underrun:1;
	unsigned int in_prebuf:1;
	unsigned int done:1;
	unsigned int killed:1;
};

void stream_free(struct stream *stream);
void stream_flush(struct stream *stream);
uint32_t stream_pop_missing(struct stream *stream);

int stream_send_underflow(struct stream *stream, int64_t offset, uint32_t underrun_for);
int stream_send_overflow(struct stream *stream);
int stream_send_killed(struct stream *stream);
int stream_send_started(struct stream *stream);
int stream_send_request(struct stream *stream);

#endif /* PULSER_SERVER_STREAM_H */
