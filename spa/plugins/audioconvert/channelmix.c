/* Spa
 *
 * Copyright © 2018 Wim Taymans
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

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <spa/support/log.h>
#include <spa/support/cpu.h>
#include <spa/utils/list.h>
#include <spa/utils/names.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/node/utils.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>
#include <spa/pod/filter.h>
#include <spa/control/control.h>
#include <spa/debug/types.h>

#define NAME "channelmix"

#define DEFAULT_RATE		44100
#define DEFAULT_CHANNELS	2

#define MAX_BUFFERS	32
#define MAX_DATAS	32

struct impl;

#define DEFAULT_MUTE	false
#define DEFAULT_VOLUME	1.0f

struct props {
	float volume;
	bool mute;
};

static void props_reset(struct props *props)
{
	props->mute = DEFAULT_MUTE;
	props->volume = DEFAULT_VOLUME;
}

struct buffer {
	uint32_t id;
#define BUFFER_FLAG_OUT		(1 << 0)
	uint32_t flags;
	struct spa_list link;
	struct spa_buffer *outbuf;
	struct spa_meta_header *h;
	void *datas[MAX_DATAS];
};

struct port {
	uint32_t direction;
	uint32_t id;

	uint64_t info_all;
	struct spa_port_info info;
	struct spa_param_info params[8];

	struct spa_io_buffers *io;
	struct spa_io_sequence *control;

	bool have_format;
	struct spa_audio_info format;
	uint32_t stride;
	uint32_t blocks;
	uint32_t size;

	struct buffer buffers[MAX_BUFFERS];
	uint32_t n_buffers;

	struct spa_list queue;
};

#include "channelmix-ops.h"

struct impl {
	struct spa_handle handle;
	struct spa_node node;

	struct spa_log *log;
	struct spa_cpu *cpu;

	struct spa_hook_list hooks;

	uint64_t info_all;
	struct spa_node_info info;
	struct props props;
	struct spa_param_info params[8];


	struct port in_port;
	struct port out_port;

	struct channelmix mix;
	unsigned int started:1;
	unsigned int is_passthrough:1;
	uint32_t cpu_flags;
};

#define CHECK_PORT(this,d,id)		(id == 0)
#define GET_IN_PORT(this,id)		(&this->in_port)
#define GET_OUT_PORT(this,id)		(&this->out_port)
#define GET_PORT(this,d,id)		(d == SPA_DIRECTION_INPUT ? GET_IN_PORT(this,id) : GET_OUT_PORT(this,id))

#define _MASK(ch)	(1ULL << SPA_AUDIO_CHANNEL_ ## ch)
#define STEREO	(_MASK(FL)|_MASK(FR))

static void emit_info(struct impl *this, bool full)
{
	if (full)
		this->info.change_mask = this->info_all;
	if (this->info.change_mask) {
		spa_node_emit_info(&this->hooks, &this->info);
		this->info.change_mask = 0;
	}
}

static void emit_params_changed(struct impl *this)
{
	this->info.change_mask |= SPA_NODE_CHANGE_MASK_PARAMS;
	this->params[1].flags ^= SPA_PARAM_INFO_SERIAL;
	emit_info(this, false);
}

static uint64_t default_mask(uint32_t channels)
{
	uint64_t mask = 0;
	switch (channels) {
	case 8:
		mask |= _MASK(RL);
		mask |= _MASK(RR);
		/* fallthrough */
	case 6:
		mask |= _MASK(SL);
		mask |= _MASK(SR);
		mask |= _MASK(LFE);
		/* fallthrough */
	case 3:
		mask |= _MASK(FC);
		/* fallthrough */
	case 2:
		mask |= _MASK(FL);
		mask |= _MASK(FR);
		break;
	case 1:
		mask |= _MASK(MONO);
		break;
	case 4:
		mask |= _MASK(FL);
		mask |= _MASK(FR);
		mask |= _MASK(RL);
		mask |= _MASK(RR);
		break;
	}
	return mask;
}

static int setup_convert(struct impl *this,
		enum spa_direction direction,
		const struct spa_audio_info *info)
{
	const struct spa_audio_info *src_info, *dst_info;
	uint32_t i, src_chan, dst_chan;
	uint64_t src_mask, dst_mask;
	int res;

	if (direction == SPA_DIRECTION_INPUT) {
		src_info = info;
		dst_info = &GET_OUT_PORT(this, 0)->format;
	} else {
		src_info = &GET_IN_PORT(this, 0)->format;
		dst_info = info;
	}

	src_chan = src_info->info.raw.channels;
	dst_chan = dst_info->info.raw.channels;

	for (i = 0, src_mask = 0; i < src_chan; i++)
		src_mask |= 1UL << src_info->info.raw.position[i];
	for (i = 0, dst_mask = 0; i < dst_chan; i++)
		dst_mask |= 1UL << dst_info->info.raw.position[i];

	if (src_mask & 1)
		src_mask = default_mask(src_chan);
	if (dst_mask & 1)
		dst_mask = default_mask(dst_chan);

	spa_log_info(this->log, NAME " %p: %s/%d@%d->%s/%d@%d %08"PRIx64":%08"PRIx64, this,
			spa_debug_type_find_name(spa_type_audio_format, src_info->info.raw.format),
			src_chan,
			src_info->info.raw.rate,
			spa_debug_type_find_name(spa_type_audio_format, dst_info->info.raw.format),
			dst_chan,
			dst_info->info.raw.rate,
			src_mask, dst_mask);

	if (src_info->info.raw.rate != dst_info->info.raw.rate)
		return -EINVAL;

	this->mix.src_chan = src_chan;
	this->mix.src_mask = src_mask;
	this->mix.dst_chan = dst_chan;
	this->mix.dst_mask = dst_mask;
	this->mix.cpu_flags = this->cpu_flags;
	this->mix.log = this->log;
	this->mix.volume = this->props.mute ? 0.0f : this->props.volume;

	if ((res = channelmix_init(&this->mix)) < 0)
		return res;

	spa_log_info(this->log, NAME " %p: got channelmix features %08x:%08x %d",
			this, this->cpu_flags, this->mix.cpu_flags,
			this->mix.is_identity);

	this->is_passthrough = this->mix.is_identity;

	return 0;
}

static int impl_node_enum_params(void *object, int seq,
				 uint32_t id, uint32_t start, uint32_t num,
				 const struct spa_pod *filter)
{
	struct impl *this = object;
	struct spa_pod *param;
	struct spa_pod_builder b = { 0 };
	uint8_t buffer[1024];
	struct spa_result_node_params result;
	uint32_t count = 0;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(num != 0, -EINVAL);

	result.id = id;
	result.next = start;
      next:
	result.index = result.next++;

	spa_pod_builder_init(&b, buffer, sizeof(buffer));

	switch (id) {
	case SPA_PARAM_PropInfo:
	{
		struct props *p = &this->props;

		switch (result.index) {
		case 0:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_PropInfo, id,
				SPA_PROP_INFO_id,   SPA_POD_Id(SPA_PROP_volume),
				SPA_PROP_INFO_name, SPA_POD_String("Volume"),
				SPA_PROP_INFO_type, SPA_POD_CHOICE_RANGE_Float(p->volume, 0.0, 10.0));
			break;
		case 1:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_PropInfo, id,
				SPA_PROP_INFO_id,   SPA_POD_Id(SPA_PROP_mute),
				SPA_PROP_INFO_name, SPA_POD_String("Mute"),
				SPA_PROP_INFO_type, SPA_POD_CHOICE_Bool(p->mute));
			break;
		default:
			return 0;
		}
		break;
	}
	case SPA_PARAM_Props:
	{
		struct props *p = &this->props;

		switch (result.index) {
		case 0:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_Props, id,
				SPA_PROP_volume,	SPA_POD_Float(p->volume),
				SPA_PROP_mute,		SPA_POD_Bool(p->mute));
			break;
		default:
			return 0;
		}
		break;
	}
	default:
		return -ENOENT;
	}

	if (spa_pod_filter(&b, &result.param, param, filter) < 0)
		goto next;

	spa_node_emit_result(&this->hooks, seq, 0, SPA_RESULT_TYPE_NODE_PARAMS, &result);

	if (++count != num)
		goto next;

	return 0;
}

static int apply_props(struct impl *this, const struct spa_pod *param)
{
	struct spa_pod_prop *prop;
	struct spa_pod_object *obj = (struct spa_pod_object *) param;
	struct props *p = &this->props;
	int changed = 0;

	SPA_POD_OBJECT_FOREACH(obj, prop) {
		switch (prop->key) {
		case SPA_PROP_volume:
			if (spa_pod_get_float(&prop->value, &p->volume) == 0)
				changed++;
			break;
		case SPA_PROP_mute:
			if (spa_pod_get_bool(&prop->value, &p->mute) == 0)
				changed++;
			break;
		default:
			break;
		}
	}
	if (changed)
		this->mix.volume = p->mute ? 0.0f : p->volume;

	return changed;
}

static int impl_node_set_io(void *object, uint32_t id, void *data, size_t size)
{
	return -ENOTSUP;
}

static int impl_node_set_param(void *object, uint32_t id, uint32_t flags,
			       const struct spa_pod *param)
{
	struct impl *this = object;

	spa_return_val_if_fail(this != NULL, -EINVAL);

	switch (id) {
	case SPA_PARAM_Props:
		if (apply_props(this, param) > 0)
			emit_params_changed(this);
		break;
	default:
		return -ENOENT;
	}
	return 0;
}

static int impl_node_send_command(void *object, const struct spa_command *command)
{
	struct impl *this = object;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(command != NULL, -EINVAL);

	switch (SPA_NODE_COMMAND_ID(command)) {
	case SPA_NODE_COMMAND_Start:
		this->started = true;
		break;
	case SPA_NODE_COMMAND_Pause:
		this->started = false;
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static void emit_port_info(struct impl *this, struct port *port, bool full)
{
	if (full)
		port->info.change_mask = port->info_all;
	if (port->info.change_mask) {
		spa_node_emit_port_info(&this->hooks,
				port->direction, port->id, &port->info);
		port->info.change_mask = 0;
	}
}

static int
impl_node_add_listener(void *object,
		struct spa_hook *listener,
		const struct spa_node_events *events,
		void *data)
{
	struct impl *this = object;
	struct spa_hook_list save;

	spa_return_val_if_fail(this != NULL, -EINVAL);

	spa_hook_list_isolate(&this->hooks, &save, listener, events, data);

	emit_info(this, true);
	emit_port_info(this, GET_IN_PORT(this, 0), true);
	emit_port_info(this, GET_OUT_PORT(this, 0), true);

	spa_hook_list_join(&this->hooks, &save);

	return 0;
}

static int
impl_node_set_callbacks(void *object,
			const struct spa_node_callbacks *callbacks,
			void *user_data)
{
	return 0;
}

static int impl_node_add_port(void *object,
		enum spa_direction direction, uint32_t port_id,
		const struct spa_dict *props)
{
	return -ENOTSUP;
}

static int impl_node_remove_port(void *object,
		enum spa_direction direction, uint32_t port_id)
{
	return -ENOTSUP;
}

static int port_enum_formats(void *object,
			     enum spa_direction direction, uint32_t port_id,
			     uint32_t index,
			     struct spa_pod **param,
			     struct spa_pod_builder *builder)
{
	struct impl *this = object;
	struct port *other;

	other = GET_PORT(this, SPA_DIRECTION_REVERSE(direction), 0);

	switch (index) {
	case 0:
		if (other->have_format) {
			*param = spa_pod_builder_add_object(builder,
				SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
				SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_audio),
				SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
				SPA_FORMAT_AUDIO_format,   SPA_POD_Id(SPA_AUDIO_FORMAT_F32P),
				SPA_FORMAT_AUDIO_rate,     SPA_POD_Int(other->format.info.raw.rate),
				SPA_FORMAT_AUDIO_channels, SPA_POD_CHOICE_RANGE_Int(DEFAULT_CHANNELS, 1, INT32_MAX));
		} else {
			*param = spa_pod_builder_add_object(builder,
				SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
				SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_audio),
				SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
				SPA_FORMAT_AUDIO_format,   SPA_POD_Id(SPA_AUDIO_FORMAT_F32P),
				SPA_FORMAT_AUDIO_rate,     SPA_POD_CHOICE_RANGE_Int(DEFAULT_RATE, 1, INT32_MAX),
				SPA_FORMAT_AUDIO_channels, SPA_POD_CHOICE_RANGE_Int(DEFAULT_CHANNELS, 1, INT32_MAX));
		}
		break;
	default:
		return 0;
	}
	return 1;
}

static int
impl_node_port_enum_params(void *object, int seq,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t id, uint32_t start, uint32_t num,
			   const struct spa_pod *filter)
{
	struct impl *this = object;
	struct port *port, *other;
	struct spa_pod *param;
	struct spa_pod_builder b = { 0 };
	uint8_t buffer[1024];
	struct spa_result_node_params result;
	uint32_t count = 0;
	int res;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(num != 0, -EINVAL);
	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	port = GET_PORT(this, direction, port_id);
	other = GET_PORT(this, SPA_DIRECTION_REVERSE(direction), port_id);

	result.id = id;
	result.next = start;
      next:
	result.index = result.next++;

	spa_pod_builder_init(&b, buffer, sizeof(buffer));

	switch (id) {
	case SPA_PARAM_EnumFormat:
		if ((res = port_enum_formats(this, direction, port_id,
						result.index, &param, &b)) <= 0)
			return res;
		break;

	case SPA_PARAM_Format:
		if (!port->have_format)
			return -EIO;
		if (result.index > 0)
			return 0;

		param = spa_format_audio_raw_build(&b, id, &port->format.info.raw);
		break;

	case SPA_PARAM_Buffers:
	{
		uint32_t buffers, size;

		if (!port->have_format)
			return -EIO;
		if (result.index > 0)
			return 0;

		if (other->n_buffers > 0) {
			buffers = other->n_buffers;
			size = other->size / other->stride;
		} else {
			buffers = 1;
			size = port->format.info.raw.rate * 1024 / DEFAULT_RATE;
		}

		param = spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_ParamBuffers, id,
			SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(buffers, 1, MAX_BUFFERS),
			SPA_PARAM_BUFFERS_blocks,  SPA_POD_Int(port->blocks),
			SPA_PARAM_BUFFERS_size,    SPA_POD_CHOICE_RANGE_Int(
							size * port->stride,
							16 * port->stride,
							INT32_MAX / port->stride),
			SPA_PARAM_BUFFERS_stride,  SPA_POD_Int(port->stride),
			SPA_PARAM_BUFFERS_align,   SPA_POD_Int(16));
		break;
	}
	case SPA_PARAM_Meta:
		switch (result.index) {
		case 0:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamMeta, id,
				SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
				SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));
			break;
		default:
			return 0;
		}
		break;

	case SPA_PARAM_IO:
		switch (result.index) {
		case 0:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamIO, id,
				SPA_PARAM_IO_id,   SPA_POD_Id(SPA_IO_Buffers),
				SPA_PARAM_IO_size, SPA_POD_Int(sizeof(struct spa_io_buffers)));
			break;
		case 1:
			param = spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamIO, id,
				SPA_PARAM_IO_id,   SPA_POD_Id(SPA_IO_Control),
				SPA_PARAM_IO_size, SPA_POD_Int(sizeof(struct spa_io_sequence)));
			break;
		default:
			return 0;
		}
		break;
	default:
		return -ENOENT;
	}

	if (spa_pod_filter(&b, &result.param, param, filter) < 0)
		goto next;

	spa_node_emit_result(&this->hooks, seq, 0, SPA_RESULT_TYPE_NODE_PARAMS, &result);

	if (++count != num)
		goto next;

	return 0;
}

static int clear_buffers(struct impl *this, struct port *port)
{
	if (port->n_buffers > 0) {
		spa_log_debug(this->log, NAME " %p: clear buffers %p", this, port);
		port->n_buffers = 0;
		spa_list_init(&port->queue);
	}
	return 0;
}

static int port_set_format(void *object,
			   enum spa_direction direction,
			   uint32_t port_id,
			   uint32_t flags,
			   const struct spa_pod *format)
{
	struct impl *this = object;
	struct port *port, *other;
	int res = 0;

	port = GET_PORT(this, direction, port_id);
	other = GET_PORT(this, SPA_DIRECTION_REVERSE(direction), port_id);

	if (format == NULL) {
		if (port->have_format) {
			port->have_format = false;
			clear_buffers(this, port);
			if (this->mix.process)
				channelmix_free(&this->mix);
		}
	} else {
		struct spa_audio_info info = { 0 };

		if ((res = spa_format_parse(format, &info.media_type, &info.media_subtype)) < 0)
			return res;

		if (info.media_type != SPA_MEDIA_TYPE_audio ||
		    info.media_subtype != SPA_MEDIA_SUBTYPE_raw)
			return -EINVAL;

		if (spa_format_audio_raw_parse(format, &info.info.raw) < 0)
			return -EINVAL;

		if (info.info.raw.format != SPA_AUDIO_FORMAT_F32P)
			return -EINVAL;

		port->stride = sizeof(float);
		port->blocks = info.info.raw.channels;

		if (other->have_format) {
			if ((res = setup_convert(this, direction, &info)) < 0)
				return res;
		}
		port->format = info;
		port->have_format = true;

		spa_log_debug(this->log, NAME " %p: set format on port %d %d", this, port_id, res);
	}

	port->info.change_mask |= SPA_PORT_CHANGE_MASK_PARAMS;
	if (port->have_format) {
		port->params[3] = SPA_PARAM_INFO(SPA_PARAM_Format, SPA_PARAM_INFO_READWRITE);
		port->params[4] = SPA_PARAM_INFO(SPA_PARAM_Buffers, SPA_PARAM_INFO_READ);
	} else {
		port->params[3] = SPA_PARAM_INFO(SPA_PARAM_Format, SPA_PARAM_INFO_WRITE);
		port->params[4] = SPA_PARAM_INFO(SPA_PARAM_Buffers, 0);
	}
	emit_port_info(this, port, false);

	return res;
}

static int
impl_node_port_set_param(void *object,
			 enum spa_direction direction, uint32_t port_id,
			 uint32_t id, uint32_t flags,
			 const struct spa_pod *param)
{
	spa_return_val_if_fail(object != NULL, -EINVAL);

	spa_return_val_if_fail(CHECK_PORT(object, direction, port_id), -EINVAL);

	if (id == SPA_PARAM_Format) {
		return port_set_format(object, direction, port_id, flags, param);
	}
	else
		return -ENOENT;
}

static int
impl_node_port_use_buffers(void *object,
		enum spa_direction direction, uint32_t port_id,
		uint32_t flags,
		struct spa_buffer **buffers, uint32_t n_buffers)
{
	struct impl *this = object;
	struct port *port;
	uint32_t i, j, size = SPA_ID_INVALID;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	port = GET_PORT(this, direction, port_id);

	spa_return_val_if_fail(port->have_format, -EIO);

	spa_log_debug(this->log, NAME " %p: use buffers %d on port %d", this, n_buffers, port_id);

	clear_buffers(this, port);

	for (i = 0; i < n_buffers; i++) {
		struct buffer *b;
		uint32_t n_datas = buffers[i]->n_datas;
		struct spa_data *d = buffers[i]->datas;

		b = &port->buffers[i];
		b->id = i;
		b->flags = 0;
		b->outbuf = buffers[i];
		b->h = spa_buffer_find_meta_data(buffers[i], SPA_META_Header, sizeof(*b->h));

		for (j = 0; j < n_datas; j++) {
			if (size == SPA_ID_INVALID)
				size = d[j].maxsize;
			else if (size != d[j].maxsize)
				return -EINVAL;

			if (!((d[j].type == SPA_DATA_MemPtr ||
			       d[j].type == SPA_DATA_MemFd ||
			       d[j].type == SPA_DATA_DmaBuf) && d[j].data != NULL)) {
				spa_log_error(this->log, NAME " %p: invalid memory on buffer %p", this,
					      buffers[i]);
				return -EINVAL;
			}
			if (!SPA_IS_ALIGNED(d[j].data, 16)) {
				spa_log_warn(this->log, NAME " %p: memory %d on buffer %d not aligned",
						this, j, i);
			}
			b->datas[j] = d[j].data;
			if (direction == SPA_DIRECTION_OUTPUT &&
			    !SPA_FLAG_CHECK(d[j].flags, SPA_DATA_FLAG_DYNAMIC))
				this->is_passthrough = false;
		}
		if (direction == SPA_DIRECTION_OUTPUT)
			spa_list_append(&port->queue, &b->link);
		else
			SPA_FLAG_SET(b->flags, BUFFER_FLAG_OUT);
	}
	port->n_buffers = n_buffers;
	port->size = size;

	return 0;
}

static int
impl_node_port_set_io(void *object,
		      enum spa_direction direction, uint32_t port_id,
		      uint32_t id, void *data, size_t size)
{
	struct impl *this = object;
	struct port *port;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	port = GET_PORT(this, direction, port_id);

	switch (id) {
	case SPA_IO_Buffers:
		port->io = data;
		break;
	case SPA_IO_Control:
		port->control = data;
		break;
	default:
		return -ENOENT;
	}
	return 0;
}

static void recycle_buffer(struct impl *this, uint32_t id)
{
	struct port *port = GET_OUT_PORT(this, 0);
	struct buffer *b = &port->buffers[id];

	if (SPA_FLAG_CHECK(b->flags, BUFFER_FLAG_OUT)) {
		spa_list_append(&port->queue, &b->link);
		SPA_FLAG_UNSET(b->flags, BUFFER_FLAG_OUT);
		spa_log_trace_fp(this->log, NAME " %p: recycle buffer %d", this, id);
	}
}

static struct buffer *dequeue_buffer(struct impl *this, struct port *port)
{
	struct buffer *b;

	if (spa_list_is_empty(&port->queue))
		return NULL;

	b = spa_list_first(&port->queue, struct buffer, link);
	spa_list_remove(&b->link);
	SPA_FLAG_SET(b->flags, BUFFER_FLAG_OUT);

	return b;
}


static int impl_node_port_reuse_buffer(void *object, uint32_t port_id, uint32_t buffer_id)
{
	struct impl *this = object;

	spa_return_val_if_fail(this != NULL, -EINVAL);
	spa_return_val_if_fail(CHECK_PORT(this, SPA_DIRECTION_OUTPUT, port_id), -EINVAL);

	recycle_buffer(this, buffer_id);

	return 0;
}

static int process_control(struct impl *this, struct port *port, struct spa_pod_sequence *sequence)
{
	struct spa_pod_control *c;

	SPA_POD_SEQUENCE_FOREACH(sequence, c) {
		switch (c->type) {
		case SPA_CONTROL_Properties:
			apply_props(this, (const struct spa_pod *) &c->value);
			break;
		default:
			break;
                }
	}
	return 0;
}

static int impl_node_process(void *object)
{
	struct impl *this = object;
	struct port *outport, *inport;
	struct spa_io_buffers *outio, *inio;
	struct buffer *sbuf, *dbuf;

	spa_return_val_if_fail(this != NULL, -EINVAL);

	outport = GET_OUT_PORT(this, 0);
	inport = GET_IN_PORT(this, 0);

	outio = outport->io;
	inio = inport->io;

	spa_return_val_if_fail(outio != NULL, -EIO);
	spa_return_val_if_fail(inio != NULL, -EIO);

	spa_log_trace_fp(this->log, NAME " %p: status %d %d", this, inio->status, outio->status);

	if (outport->control)
		process_control(this, outport, &outport->control->sequence);

	if (outio->status == SPA_STATUS_HAVE_BUFFER)
		goto done;

	if (inio->status != SPA_STATUS_HAVE_BUFFER)
		return SPA_STATUS_NEED_BUFFER;

	/* recycle */
	if (outio->buffer_id < outport->n_buffers) {
		recycle_buffer(this, outio->buffer_id);
		outio->buffer_id = SPA_ID_INVALID;
	}

	if (inio->buffer_id >= inport->n_buffers)
		return inio->status = -EINVAL;

	if ((dbuf = dequeue_buffer(this, outport)) == NULL)
		return outio->status = -EPIPE;

	sbuf = &inport->buffers[inio->buffer_id];

	{
		uint32_t i, n_samples;
		struct spa_buffer *sb = sbuf->outbuf, *db = dbuf->outbuf;
		uint32_t n_src_datas = sb->n_datas;
		uint32_t n_dst_datas = db->n_datas;
		const void *src_datas[n_src_datas];
		void *dst_datas[n_dst_datas];
		bool is_passthrough;

		is_passthrough = this->is_passthrough &&
			this->mix.volume == 1.0f &&
			this->mix.is_identity;

		n_samples = sb->datas[0].chunk->size / inport->stride;

		for (i = 0; i < n_src_datas; i++)
			src_datas[i] = sb->datas[i].data;
		for (i = 0; i < n_dst_datas; i++) {
			dst_datas[i] = is_passthrough ? (void*)src_datas[i] : dbuf->datas[i];
			db->datas[i].data = dst_datas[i];
			db->datas[i].chunk->size = n_samples * outport->stride;
		}

		if (!is_passthrough)
			channelmix_process(&this->mix, n_dst_datas, dst_datas,
				    n_src_datas, src_datas, n_samples);
	}

	outio->status = SPA_STATUS_HAVE_BUFFER;
	outio->buffer_id = dbuf->id;

	inio->status = SPA_STATUS_NEED_BUFFER;

      done:
	return SPA_STATUS_HAVE_BUFFER | SPA_STATUS_NEED_BUFFER;
}

static const struct spa_node_methods impl_node = {
	SPA_VERSION_NODE_METHODS,
	.add_listener = impl_node_add_listener,
	.set_callbacks = impl_node_set_callbacks,
	.enum_params = impl_node_enum_params,
	.set_param = impl_node_set_param,
	.set_io = impl_node_set_io,
	.send_command = impl_node_send_command,
	.add_port = impl_node_add_port,
	.remove_port = impl_node_remove_port,
	.port_enum_params = impl_node_port_enum_params,
	.port_set_param = impl_node_port_set_param,
	.port_use_buffers = impl_node_port_use_buffers,
	.port_set_io = impl_node_port_set_io,
	.port_reuse_buffer = impl_node_port_reuse_buffer,
	.process = impl_node_process,
};

static int impl_get_interface(struct spa_handle *handle, uint32_t type, void **interface)
{
	struct impl *this;

	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	this = (struct impl *) handle;

	if (type == SPA_TYPE_INTERFACE_Node)
		*interface = &this->node;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	return 0;
}

static size_t
impl_get_size(const struct spa_handle_factory *factory,
	      const struct spa_dict *params)
{
	return sizeof(struct impl);
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	struct impl *this;
	struct port *port;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	this = (struct impl *) handle;

	for (i = 0; i < n_support; i++) {
		switch (support[i].type) {
		case SPA_TYPE_INTERFACE_Log:
			this->log = support[i].data;
			break;
		case SPA_TYPE_INTERFACE_CPU:
			this->cpu = support[i].data;
			break;
		}
	}

	if (this->cpu)
		this->cpu_flags = spa_cpu_get_flags(this->cpu);

	spa_hook_list_init(&this->hooks);

	this->node.iface = SPA_INTERFACE_INIT(
			SPA_TYPE_INTERFACE_Node,
			SPA_VERSION_NODE,
			&impl_node, this);
	this->info_all = SPA_NODE_CHANGE_MASK_FLAGS |
			SPA_NODE_CHANGE_MASK_PARAMS;
	this->info = SPA_NODE_INFO_INIT();
	this->info.flags = SPA_NODE_FLAG_RT;
	this->params[0] = SPA_PARAM_INFO(SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ);
	this->params[1] = SPA_PARAM_INFO(SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE);
	this->info.params = this->params;
	this->info.n_params = 2;
	props_reset(&this->props);

	port = GET_OUT_PORT(this, 0);
	port->direction = SPA_DIRECTION_OUTPUT;
	port->id = 0;
	port->info_all = SPA_PORT_CHANGE_MASK_FLAGS |
			SPA_PORT_CHANGE_MASK_PARAMS;
	port->info = SPA_PORT_INFO_INIT();
	port->info.flags = SPA_PORT_FLAG_DYNAMIC_DATA;
	port->params[0] = SPA_PARAM_INFO(SPA_PARAM_EnumFormat, SPA_PARAM_INFO_READ);
	port->params[1] = SPA_PARAM_INFO(SPA_PARAM_Meta, SPA_PARAM_INFO_READ);
	port->params[2] = SPA_PARAM_INFO(SPA_PARAM_IO, SPA_PARAM_INFO_READ);
	port->params[3] = SPA_PARAM_INFO(SPA_PARAM_Format, SPA_PARAM_INFO_WRITE);
	port->params[4] = SPA_PARAM_INFO(SPA_PARAM_Buffers, 0);
	port->info.params = port->params;
	port->info.n_params = 5;
	spa_list_init(&port->queue);

	port = GET_IN_PORT(this, 0);
	port->direction = SPA_DIRECTION_INPUT;
	port->id = 0;
	port->info_all = SPA_PORT_CHANGE_MASK_FLAGS |
			SPA_PORT_CHANGE_MASK_PARAMS;
	port->info = SPA_PORT_INFO_INIT();
	port->info.flags = SPA_PORT_FLAG_NO_REF |
		SPA_PORT_FLAG_DYNAMIC_DATA;
	port->params[0] = SPA_PARAM_INFO(SPA_PARAM_EnumFormat, SPA_PARAM_INFO_READ);
	port->params[1] = SPA_PARAM_INFO(SPA_PARAM_Meta, SPA_PARAM_INFO_READ);
	port->params[2] = SPA_PARAM_INFO(SPA_PARAM_IO, SPA_PARAM_INFO_READ);
	port->params[3] = SPA_PARAM_INFO(SPA_PARAM_Format, SPA_PARAM_INFO_WRITE);
	port->params[4] = SPA_PARAM_INFO(SPA_PARAM_Buffers, 0);
	port->info.params = port->params;
	port->info.n_params = 0;
	spa_list_init(&port->queue);

	return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE_INTERFACE_Node,},
};

static int
impl_enum_interface_info(const struct spa_handle_factory *factory,
			 const struct spa_interface_info **info,
			 uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*info = &impl_interfaces[*index];
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}

const struct spa_handle_factory spa_channelmix_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	SPA_NAME_AUDIO_PROCESS_CHANNELMIX,
	NULL,
	impl_get_size,
	impl_init,
	impl_enum_interface_info,
};
