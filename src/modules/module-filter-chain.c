/* PipeWire
 *
 * Copyright © 2021 Wim Taymans
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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>

#include "config.h"

#include "module-filter-chain/ladspa.h"

#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/utils/json.h>
#include <spa/param/profiler.h>
#include <spa/debug/pod.h>

#include <pipewire/utils.h>
#include <pipewire/private.h>
#include <pipewire/impl.h>
#include <pipewire/extensions/profiler.h>

#define NAME "filter-chain"

/**
 * \page page_module_filter_chain PipeWire Module: Filter-Chain
 *
 */
static const struct spa_dict_item module_props[] = {
	{ PW_KEY_MODULE_AUTHOR, "Wim Taymans <wim.taymans@gmail.com>" },
	{ PW_KEY_MODULE_DESCRIPTION, "Create filter chain streams" },
	{ PW_KEY_MODULE_USAGE, " [ remote.name=<remote> ] "
				"[ node.latency=<latency as fraction> ] "
				"[ node.name=<name of the nodes> ] "
				"[ node.description=<description of the nodes> ] "
				"[ audio.rate=<sample rate> ] "
				"[ audio.channels=<number of channels> ] "
				"[ audio.position=<channel map> ] "
				"filter.graph = [ "
				"    nodes = [ "
				"        { "
				"          type = ladspa "
				"          name = <name> "
				"          plugin = <plugin> "
				"          label = <label> "
				"          control = { "
				"             <controlname> = <value> ... "
				"          } "
				"        } "
				"    ] "
				"    links = [ "
				"        { output = <portname> input = <portname> } ... "
				"    ] "
				"    inputs = [ <portname> ... ] "
				"    outputs = [ <portname> ... ] "
				"] "
				"[ capture.props=<properties> ] "
				"[ playback.props=<properties> ] " },
	{ PW_KEY_MODULE_VERSION, PACKAGE_VERSION },
};

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>

#include <spa/utils/result.h>
#include <spa/pod/builder.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>

#include <pipewire/pipewire.h>

#define MAX_HNDL 64
#define MAX_PORTS 64
#define MAX_CONTROLS 256
#define MAX_SAMPLES 8192

struct ladspa_handle {
	struct spa_list link;
	int ref;
	char path[PATH_MAX];
	void *handle;
	LADSPA_Descriptor_Function desc_func;
	struct spa_list descriptor_list;
};

struct ladspa_descriptor {
	struct spa_list link;
	int ref;
	struct ladspa_handle *handle;
	char label[256];
	const LADSPA_Descriptor *desc;

	uint32_t n_input;
	uint32_t n_output;
	uint32_t n_control;
	uint32_t n_notify;
	unsigned long input[MAX_PORTS];
	unsigned long output[MAX_PORTS];
	unsigned long control[MAX_PORTS];
	unsigned long notify[MAX_PORTS];
	LADSPA_Data default_control[MAX_PORTS];
};

struct port {
	struct spa_list link;
	struct node *node;

	uint32_t idx;
	unsigned long p;

	struct spa_list link_list;
	uint32_t n_links;
	uint32_t external;

	LADSPA_Data control_data;
	LADSPA_Data *audio_data[MAX_HNDL];
};

struct node {
	struct spa_list link;
	struct graph *graph;

	struct ladspa_descriptor *desc;

	char name[256];

	struct port input_port[MAX_PORTS];
	struct port output_port[MAX_PORTS];
	struct port control_port[MAX_PORTS];
	struct port notify_port[MAX_PORTS];

	uint32_t n_hndl;
	LADSPA_Handle hndl[MAX_HNDL];

	unsigned int n_deps;
	unsigned int visited:1;
};

struct link {
	struct spa_list link;

	struct spa_list input_link;
	struct spa_list output_link;

	struct port *output;
	struct port *input;
};

struct graph_port {
	const LADSPA_Descriptor *desc;
	LADSPA_Handle hndl;
	uint32_t port;
};

struct graph_hndl {
	const LADSPA_Descriptor *desc;
	LADSPA_Handle hndl;
};

struct graph {
	struct impl *impl;

	struct spa_list node_list;
	struct spa_list link_list;

	uint32_t n_input;
	struct graph_port input[MAX_PORTS];

	uint32_t n_output;
	struct graph_port output[MAX_PORTS];

	uint32_t n_hndl;
	struct graph_hndl hndl[MAX_HNDL];

	uint32_t n_control;
	struct port *control_port[MAX_CONTROLS];

	LADSPA_Data silence_data[MAX_SAMPLES];
	LADSPA_Data discard_data[MAX_SAMPLES];
};

struct impl {
	struct pw_context *context;

	struct pw_impl_module *module;
	struct pw_work_queue *work;

	struct spa_hook module_listener;

	struct pw_core *core;
	struct spa_hook core_proxy_listener;
	struct spa_hook core_listener;

	struct spa_list ladspa_handle_list;

	struct pw_properties *capture_props;
	struct pw_stream *capture;
	struct spa_hook capture_listener;
	struct spa_audio_info_raw capture_info;

	struct pw_properties *playback_props;
	struct pw_stream *playback;
	struct spa_hook playback_listener;
	struct spa_audio_info_raw playback_info;

	unsigned int do_disconnect:1;
	unsigned int unloading:1;

	uint32_t rate;

	struct graph graph;
};

#include "module-filter-chain/builtin.h"

static void do_unload_module(void *obj, void *data, int res, uint32_t id)
{
	struct impl *impl = data;
	pw_impl_module_destroy(impl->module);
}
static void unload_module(struct impl *impl)
{
	if (!impl->unloading) {
		impl->unloading = true;
		pw_work_queue_add(impl->work, impl, 0, do_unload_module, impl);
	}
}

static void capture_destroy(void *d)
{
	struct impl *impl = d;
	spa_hook_remove(&impl->capture_listener);
	impl->capture = NULL;
}

static void capture_process(void *d)
{
	struct impl *impl = d;
	struct pw_buffer *in, *out;
	struct graph *graph = &impl->graph;
	uint32_t i, size = 0, n_hndl = graph->n_hndl;
	int32_t stride = 0;

	if ((in = pw_stream_dequeue_buffer(impl->capture)) == NULL)
		pw_log_debug("out of capture buffers: %m");

	if ((out = pw_stream_dequeue_buffer(impl->playback)) == NULL)
		pw_log_debug("out of playback buffers: %m");

	if (in == NULL || out == NULL)
		goto done;

	for (i = 0; i < in->buffer->n_datas; i++) {
		struct spa_data *ds = &in->buffer->datas[i];
		struct graph_port *port = &graph->input[i];
		if (port->desc)
			port->desc->connect_port(port->hndl, port->port,
				SPA_MEMBER(ds->data, ds->chunk->offset, void));
		size = SPA_MAX(size, ds->chunk->size);
		stride = SPA_MAX(stride, ds->chunk->stride);
	}
	for (i = 0; i < out->buffer->n_datas; i++) {
		struct spa_data *dd = &out->buffer->datas[i];
		struct graph_port *port = &graph->output[i];
		if (port->desc)
			port->desc->connect_port(port->hndl, port->port, dd->data);
		else
			memset(dd->data, 0, size);
		dd->chunk->offset = 0;
		dd->chunk->size = size;
		dd->chunk->stride = stride;
	}
	for (i = 0; i < n_hndl; i++) {
		struct graph_hndl *hndl = &graph->hndl[i];
		hndl->desc->run(hndl->hndl, size / sizeof(float));
	}

done:
	if (in != NULL)
		pw_stream_queue_buffer(impl->capture, in);
	if (out != NULL)
		pw_stream_queue_buffer(impl->playback, out);
}

static float get_default(struct impl *impl, struct ladspa_descriptor *desc, uint32_t p)
{
	const LADSPA_Descriptor *d = desc->desc;
	LADSPA_PortRangeHintDescriptor hint = d->PortRangeHints[p].HintDescriptor;
	LADSPA_Data lower, upper, def;

	lower = d->PortRangeHints[p].LowerBound;
	upper = d->PortRangeHints[p].UpperBound;

	if (LADSPA_IS_HINT_SAMPLE_RATE(hint)) {
		lower *= (LADSPA_Data) impl->rate;
		upper *= (LADSPA_Data) impl->rate;
	}

	switch (hint & LADSPA_HINT_DEFAULT_MASK) {
	case LADSPA_HINT_DEFAULT_MINIMUM:
		def = lower;
		break;
	case LADSPA_HINT_DEFAULT_MAXIMUM:
		def = upper;
		break;
	case LADSPA_HINT_DEFAULT_LOW:
		if (LADSPA_IS_HINT_LOGARITHMIC(hint))
			def = (LADSPA_Data) exp(log(lower) * 0.75 + log(upper) * 0.25);
		else
			def = (LADSPA_Data) (lower * 0.75 + upper * 0.25);
		break;
	case LADSPA_HINT_DEFAULT_MIDDLE:
		if (LADSPA_IS_HINT_LOGARITHMIC(hint))
			def = (LADSPA_Data) exp(log(lower) * 0.5 + log(upper) * 0.5);
		else
			def = (LADSPA_Data) (lower * 0.5 + upper * 0.5);
		break;
	case LADSPA_HINT_DEFAULT_HIGH:
		if (LADSPA_IS_HINT_LOGARITHMIC(hint))
			def = (LADSPA_Data) exp(log(lower) * 0.25 + log(upper) * 0.75);
		else
			def = (LADSPA_Data) (lower * 0.25 + upper * 0.75);
		break;
	case LADSPA_HINT_DEFAULT_0:
		def = 0;
		break;
	case LADSPA_HINT_DEFAULT_1:
		def = 1;
		break;
	case LADSPA_HINT_DEFAULT_100:
		def = 100;
		break;
	case LADSPA_HINT_DEFAULT_440:
		def = 440;
		break;
	default:
		if (upper == lower)
			def = upper;
		else
			def = SPA_CLAMP(0.5 * upper, lower, upper);
		break;
	}
	if (LADSPA_IS_HINT_INTEGER(hint))
		def = roundf(def);
	return def;
}

static struct node *find_node(struct graph *graph, const char *name)
{
	struct node *node;
	spa_list_for_each(node, &graph->node_list, link) {
		if (spa_streq(node->name, name))
			return node;
	}
	return NULL;
}

static struct port *find_port(struct node *node, const char *name, int descriptor)
{
	char *col, *node_name, *port_name, *str;
	struct port *ports;
	const LADSPA_Descriptor *d;
	uint32_t i, n_ports;

	str = strdupa(name);
	col = strchr(str, ':');
	if (col != NULL) {
		node_name = str;
		port_name = col + 1;
		*col = '\0';
		node = find_node(node->graph, node_name);
	} else {
		node_name = node->name;
		port_name = str;
	}
	if (node == NULL)
		return NULL;

	if (LADSPA_IS_PORT_INPUT(descriptor)) {
		if (LADSPA_IS_PORT_CONTROL(descriptor)) {
			ports = node->control_port;
			n_ports = node->desc->n_control;
		} else {
			ports = node->input_port;
			n_ports = node->desc->n_input;
		}
	} else if (LADSPA_IS_PORT_OUTPUT(descriptor)) {
		if (LADSPA_IS_PORT_CONTROL(descriptor)) {
			ports = node->notify_port;
			n_ports = node->desc->n_notify;
		} else {
			ports = node->output_port;
			n_ports = node->desc->n_output;
		}
	} else
		return NULL;

	d = node->desc->desc;
	for (i = 0; i < n_ports; i++) {
		struct port *port = &ports[i];
		if (spa_streq(d->PortNames[port->p], port_name))
			return port;
	}
	return NULL;
}

static struct spa_pod *get_prop_info(struct graph *graph, struct spa_pod_builder *b, uint32_t idx)
{
	struct spa_pod_frame f[2];
	struct impl *impl = graph->impl;
	struct port *port = graph->control_port[idx];
	struct node *node = port->node;
	struct ladspa_descriptor *desc = node->desc;
	uint32_t p = port->p;
	const LADSPA_Descriptor *d = desc->desc;
	LADSPA_PortRangeHintDescriptor hint = d->PortRangeHints[p].HintDescriptor;
	float def, upper, lower;
	char name[512];

	def = get_default(impl, desc, p);
	lower = d->PortRangeHints[p].LowerBound;
	upper = d->PortRangeHints[p].UpperBound;

	if (LADSPA_IS_HINT_SAMPLE_RATE(hint)) {
		lower *= (LADSPA_Data) impl->rate;
		upper *= (LADSPA_Data) impl->rate;
	}

	if (node->name[0] != '\0')
		snprintf(name, sizeof(name), "%s:%s", node->name, d->PortNames[p]);
	else
		snprintf(name, sizeof(name), "%s", d->PortNames[p]);

	spa_pod_builder_push_object(b, &f[0],
			SPA_TYPE_OBJECT_PropInfo, SPA_PARAM_PropInfo);
	spa_pod_builder_add (b,
			SPA_PROP_INFO_id, SPA_POD_Id(SPA_PROP_START_CUSTOM + idx),
			SPA_PROP_INFO_name, SPA_POD_String(name),
			0);
	spa_pod_builder_prop(b, SPA_PROP_INFO_type, 0);
	if (lower == upper) {
		spa_pod_builder_float(b, def);
	} else {
		spa_pod_builder_push_choice(b, &f[1], SPA_CHOICE_Range, 0);
		spa_pod_builder_float(b, def);
		spa_pod_builder_float(b, lower);
		spa_pod_builder_float(b, upper);
		spa_pod_builder_pop(b, &f[1]);
	}
	spa_pod_builder_prop(b, SPA_PROP_INFO_params, 0);
	spa_pod_builder_bool(b, true);
	return spa_pod_builder_pop(b, &f[0]);
}

static struct spa_pod *get_props_param(struct graph *graph, struct spa_pod_builder *b)
{
	struct spa_pod_frame f[2];
	uint32_t i;
	char name[512];

	spa_pod_builder_push_object(b, &f[0],
			SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	spa_pod_builder_prop(b, SPA_PROP_params, 0);
	spa_pod_builder_push_struct(b, &f[1]);

	for (i = 0; i < graph->n_control; i++) {
		struct port *port = graph->control_port[i];
		struct node *node = port->node;
		struct ladspa_descriptor *desc = node->desc;
		const LADSPA_Descriptor *d = desc->desc;

		if (node->name[0] != '\0')
			snprintf(name, sizeof(name), "%s:%s", node->name, d->PortNames[port->p]);
		else
			snprintf(name, sizeof(name), "%s", d->PortNames[port->p]);

		spa_pod_builder_string(b, name);
		spa_pod_builder_float(b, port->control_data);
	}
	spa_pod_builder_pop(b, &f[1]);
	return spa_pod_builder_pop(b, &f[0]);
}

static int set_control_value(struct node *node, const char *name, float *value)
{
	struct ladspa_descriptor *desc;
	struct port *port;
	float old;

	port = find_port(node, name, LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL);
	if (port == NULL)
		return 0;

	node = port->node;
	desc = node->desc;

	old = port->control_data;
	port->control_data = value ? *value : desc->default_control[port->idx];
	pw_log_info("control %d ('%s') from %f to %f", port->idx, name, old, port->control_data);
	return old == port->control_data ? 0 : 1;
}

static int parse_params(struct graph *graph, const struct spa_pod *pod)
{
	struct spa_pod_parser prs;
	struct spa_pod_frame f;
	int changed = 0;
	struct node *def_node;

	def_node = spa_list_first(&graph->node_list, struct node, link);

	spa_pod_parser_pod(&prs, pod);
	if (spa_pod_parser_push_struct(&prs, &f) < 0)
		return 0;

	while (true) {
		const char *name;
		float value, *val = NULL;

		if (spa_pod_parser_get_string(&prs, &name) < 0)
			break;
		if (spa_pod_parser_get_float(&prs, &value) >= 0)
			val = &value;

		changed += set_control_value(def_node, name, val);
	}
	return changed;
}

static void graph_reset(struct graph *graph)
{
	uint32_t i;
	for (i = 0; i < graph->n_hndl; i++) {
		struct graph_hndl *hndl = &graph->hndl[i];
		const LADSPA_Descriptor *d = hndl->desc;
		if (d->deactivate)
			d->deactivate(hndl->hndl);
		if (d->activate)
			d->activate(hndl->hndl);
	}
}

static void param_props_changed(struct impl *impl, const struct spa_pod *param)
{
	struct spa_pod_object *obj = (struct spa_pod_object *) param;
	const struct spa_pod_prop *prop;
	struct graph *graph = &impl->graph;
	int changed = 0;

	SPA_POD_OBJECT_FOREACH(obj, prop) {
		uint32_t idx;
		float value;
		struct port *port;

		if (prop->key == SPA_PROP_params) {
			changed += parse_params(graph, &prop->value);
			continue;
		}
		if (prop->key < SPA_PROP_START_CUSTOM)
			continue;
		idx = prop->key - SPA_PROP_START_CUSTOM;
		if (idx >= graph->n_control)
			continue;

		if (spa_pod_get_float(&prop->value, &value) < 0)
			continue;

		port = graph->control_port[idx];

		if (port->control_data != value) {
			port->control_data = value;
			changed++;
			pw_log_info("control %d to %f", idx, port->control_data);
		}
	}
	if (changed > 0) {
		uint8_t buffer[1024];
		struct spa_pod_builder b;
		const struct spa_pod *params[1];

		spa_pod_builder_init(&b, buffer, sizeof(buffer));
		params[0] = get_props_param(graph, &b);

		pw_stream_update_params(impl->capture, params, 1);
	}
}

static void param_latency_changed(struct impl *impl, const struct spa_pod *param)
{
	struct spa_latency_info latency;
	uint8_t buffer[1024];
	struct spa_pod_builder b;
	const struct spa_pod *params[1];

	if (spa_latency_parse(param, &latency) < 0)
		return;

	spa_pod_builder_init(&b, buffer, sizeof(buffer));
	params[0] = spa_latency_build(&b, SPA_PARAM_Latency, &latency);

	if (latency.direction == SPA_DIRECTION_INPUT)
		pw_stream_update_params(impl->capture, params, 1);
	else
		pw_stream_update_params(impl->playback, params, 1);
}

static void param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
	struct impl *impl = data;
	struct graph *graph = &impl->graph;

	switch (id) {
	case SPA_PARAM_Format:
		if (param == NULL)
			graph_reset(graph);
		break;
	case SPA_PARAM_Props:
		if (param != NULL)
			param_props_changed(impl, param);
		break;
	case SPA_PARAM_Latency:
		param_latency_changed(impl, param);
		break;
	}
}

static const struct pw_stream_events in_stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.destroy = capture_destroy,
	.process = capture_process,
	.param_changed = param_changed
};

static void playback_destroy(void *d)
{
	struct impl *impl = d;
	spa_hook_remove(&impl->playback_listener);
	impl->playback = NULL;
}

static const struct pw_stream_events out_stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.destroy = playback_destroy,
	.param_changed = param_changed
};

static int builder_overflow(void *data, uint32_t size)
{
	struct spa_pod_builder *b = data;
	b->size = SPA_ROUND_UP_N(size, 4096);
	if ((b->data = realloc(b->data, b->size)) == NULL)
		return -errno;
        return 0;
}

static const struct spa_pod_builder_callbacks builder_callbacks = {
	SPA_VERSION_POD_BUILDER_CALLBACKS,
	.overflow = builder_overflow
};

static int setup_streams(struct impl *impl)
{
	int res;
	uint32_t i, n_params;
	const struct spa_pod *params[256];
	struct spa_pod_builder b;
	struct graph *graph = &impl->graph;

	impl->capture = pw_stream_new(impl->core,
			"filter capture", impl->capture_props);
	impl->capture_props = NULL;
	if (impl->capture == NULL)
		return -errno;

	pw_stream_add_listener(impl->capture,
			&impl->capture_listener,
			&in_stream_events, impl);

	impl->playback = pw_stream_new(impl->core,
			"filter playback", impl->playback_props);
	impl->playback_props = NULL;
	if (impl->playback == NULL)
		return -errno;

	pw_stream_add_listener(impl->playback,
			&impl->playback_listener,
			&out_stream_events, impl);

	n_params = 0;
	spa_pod_builder_init(&b, NULL, 0);
	spa_pod_builder_set_callbacks(&b, &builder_callbacks, &b);

	params[n_params++] = spa_format_audio_raw_build(&b,
			SPA_PARAM_EnumFormat, &impl->capture_info);

	for (i = 0; i < graph->n_control; i++)
		params[n_params++] = get_prop_info(graph, &b, i);

	params[n_params++] = get_props_param(graph, &b);

	res = pw_stream_connect(impl->capture,
			PW_DIRECTION_INPUT,
			PW_ID_ANY,
			PW_STREAM_FLAG_AUTOCONNECT |
			PW_STREAM_FLAG_MAP_BUFFERS |
			PW_STREAM_FLAG_RT_PROCESS,
			params, n_params);
	free(b.data);
	if (res < 0)
		return res;

	n_params = 0;
	spa_pod_builder_init(&b, NULL, 0);
	spa_pod_builder_set_callbacks(&b, &builder_callbacks, &b);
	params[n_params++] = spa_format_audio_raw_build(&b,
			SPA_PARAM_EnumFormat, &impl->playback_info);

	res = pw_stream_connect(impl->playback,
			PW_DIRECTION_OUTPUT,
			PW_ID_ANY,
			PW_STREAM_FLAG_AUTOCONNECT |
			PW_STREAM_FLAG_MAP_BUFFERS |
			PW_STREAM_FLAG_RT_PROCESS,
			params, n_params);
	free(b.data);

	if (res < 0)
		return res;


	return 0;
}

static const LADSPA_Descriptor *find_descriptor(LADSPA_Descriptor_Function desc_func,
		const char *label)
{
	unsigned long i;

	for (i = 0; ;i++) {
		const LADSPA_Descriptor *desc = desc_func(i);
		if (desc == NULL)
			break;
		if (spa_streq(desc->Label, label))
			return desc;
	}
	return NULL;
}

static uint32_t count_array(struct spa_json *json)
{
	struct spa_json it = *json;
	char v[256];
	uint32_t count = 0;
	while (spa_json_get_string(&it, v, sizeof(v)) > 0)
		count++;
	return count;
}

static void ladspa_handle_unref(struct ladspa_handle *hndl)
{
	if (--hndl->ref > 0)
		return;

	if (hndl->handle)
		dlclose(hndl->handle);

	spa_list_remove(&hndl->link);
	free(hndl);
}

static struct ladspa_handle *ladspa_handle_load_by_path(struct impl *impl, const char *path)
{
	struct ladspa_handle *hndl;
	int res;

	spa_list_for_each(hndl, &impl->ladspa_handle_list, link) {
		if (spa_streq(hndl->path, path)) {
			hndl->ref++;
			return hndl;
		}
	}

	hndl = calloc(1, sizeof(*hndl));
	if (!hndl)
		return NULL;

	hndl->ref = 1;
	snprintf(hndl->path, sizeof(hndl->path), "%s", path);

	if (!spa_streq(path, "builtin")) {
		hndl->handle = dlopen(path, RTLD_NOW);
		if (!hndl->handle) {
			pw_log_debug("failed to open '%s': %s", path, dlerror());
			res = -ENOENT;
			goto exit;
		}

		pw_log_info("successfully opened '%s'", path);

		hndl->desc_func = (LADSPA_Descriptor_Function) dlsym(hndl->handle, "ladspa_descriptor");
		if (!hndl->desc_func) {
			pw_log_warn("cannot find descriptor function in '%s': %s", path, dlerror());
			res = -ENOSYS;
			goto exit;
		}
	}
	else {
		hndl->desc_func = builtin_ladspa_descriptor;
	}

	spa_list_init(&hndl->descriptor_list);
	spa_list_append(&impl->ladspa_handle_list, &hndl->link);

	return hndl;

exit:
	if (hndl->handle)
		dlclose(hndl->handle);

	free(hndl);
	errno = -res;

	return NULL;
}

static struct ladspa_handle *ladspa_handle_load(struct impl *impl, const char *plugin)
{
	struct ladspa_handle *hndl = NULL;

	if (!spa_streq(plugin, "builtin") && plugin[0] != '/') {
		const char *search_dirs, *p;
		char path[PATH_MAX];
		size_t len;

		search_dirs = getenv("LADSPA_PATH");
		if (!search_dirs)
			search_dirs = "/usr/lib64/ladspa";

		/*
		 * set the errno for the case when `ladspa_handle_load_by_path()`
		 * is never called, which can only happen if the supplied
		 * LADSPA_PATH contains too long paths
		 */
		errno = ENAMETOOLONG;

		while ((p = pw_split_walk(NULL, ":", &len, &search_dirs))) {
			int pathlen;

			if (len >= sizeof(path))
				continue;

			pathlen = snprintf(path, sizeof(path), "%.*s/%s.so", (int) len, p, plugin);
			if (pathlen < 0 || (size_t) pathlen >= sizeof(path))
				continue;

			hndl = ladspa_handle_load_by_path(impl, path);
			if (hndl)
				break;
		}
	}
	else {
		hndl = ladspa_handle_load_by_path(impl, plugin);
	}

	if (!hndl)
		pw_log_error("failed to load plugin '%s': %s", plugin, strerror(errno));

	return hndl;
}

static void ladspa_descriptor_unref(struct ladspa_descriptor *desc)
{
	if (--desc->ref > 0)
		return;

	spa_list_remove(&desc->link);
	ladspa_handle_unref(desc->handle);
	free(desc);
}

static struct ladspa_descriptor *ladspa_descriptor_load(struct impl *impl,
		const char *plugin, const char *label)
{
	struct ladspa_handle *hndl;
	struct ladspa_descriptor *desc;
	const LADSPA_Descriptor *d;
	uint32_t i;
	unsigned long p;
	int res;

	if ((hndl = ladspa_handle_load(impl, plugin)) == NULL)
		return NULL;

	spa_list_for_each(desc, &hndl->descriptor_list, link) {
		if (spa_streq(desc->label, label)) {
			desc->ref++;

			/*
			 * since ladspa_handle_load() increments the reference count of the handle,
			 * if the descriptor is found, then the handle's reference count
			 * has already been incremented to account for the descriptor,
			 * so we need to unref handle here since we're merely reusing
			 * thedescriptor, not creating a new one
			 */
			ladspa_handle_unref(hndl);
			return desc;
		}
	}

	desc = calloc(1, sizeof(*desc));
	desc->ref = 1;
	desc->handle = hndl;

	if ((d = find_descriptor(hndl->desc_func, label)) == NULL) {
		pw_log_error("cannot find label %s", label);
		res = -ENOENT;
		goto exit;
	}
	desc->desc = d;
	snprintf(desc->label, sizeof(desc->label), "%s", label);

	for (p = 0; p < d->PortCount; p++) {
		if (LADSPA_IS_PORT_AUDIO(d->PortDescriptors[p])) {
			if (LADSPA_IS_PORT_INPUT(d->PortDescriptors[p])) {
				pw_log_info("using port %lu ('%s') as input %d", p,
						d->PortNames[p], desc->n_input);
				desc->input[desc->n_input++] = p;
			}
			else if (LADSPA_IS_PORT_OUTPUT(d->PortDescriptors[p])) {
				pw_log_info("using port %lu ('%s') as output %d", p,
						d->PortNames[p], desc->n_output);
				desc->output[desc->n_output++] = p;
			}
		} else if (LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p])) {
			if (LADSPA_IS_PORT_INPUT(d->PortDescriptors[p])) {
				pw_log_info("using port %lu ('%s') as control %d", p,
						d->PortNames[p], desc->n_control);
				desc->control[desc->n_control++] = p;
			}
			else if (LADSPA_IS_PORT_OUTPUT(d->PortDescriptors[p])) {
				pw_log_info("using port %lu ('%s') as notify %d", p,
						d->PortNames[p], desc->n_notify);
				desc->notify[desc->n_notify++] = p;
			}
		}
	}
	if (desc->n_input == 0 && desc->n_output == 0) {
		pw_log_error("plugin has no input and no output ports");
		res = -ENOTSUP;
		goto exit;
	}
	for (i = 0; i < desc->n_control; i++) {
		p = desc->control[i];
		desc->default_control[i] = get_default(impl, desc, p);
		pw_log_info("control %d ('%s') default to %f", i,
				d->PortNames[p], desc->default_control[i]);
	}
	spa_list_append(&hndl->descriptor_list, &desc->link);

	return desc;

exit:
	if (hndl != NULL)
		ladspa_handle_unref(hndl);
	free(desc);
	errno = -res;
	return NULL;
}

/**
 * {
 *   "Reverb tail" = 2.0
 *   ...
 * }
 */
static int parse_control(struct node *node, struct spa_json *control)
{
	struct spa_json it[1];
	char key[256];

        if (spa_json_enter_object(control, &it[0]) <= 0)
		return -EINVAL;

	while (spa_json_get_string(&it[0], key, sizeof(key)) > 0) {
		float fl;
		if (spa_json_get_float(&it[0], &fl) <= 0)
			break;
		set_control_value(node, key, &fl);
	}
	return 0;
}

/**
 * output = [name:][portname]
 * input = [name:][portname]
 * ...
 */
static int parse_link(struct graph *graph, struct spa_json *json)
{
	char key[256];
	char output[256] = "";
	char input[256] = "";
	const char *val;
	struct node *def_node;
	struct port *in_port, *out_port;
	struct link *link;

	while (spa_json_get_string(json, key, sizeof(key)) > 0) {
		if (spa_streq(key, "output")) {
			if (spa_json_get_string(json, output, sizeof(output)) <= 0) {
				pw_log_error("output expects a string");
				return -EINVAL;
			}
		}
		else if (spa_streq(key, "input")) {
			if (spa_json_get_string(json, input, sizeof(input)) <= 0) {
				pw_log_error("input expects a string");
				return -EINVAL;
			}
		}
		else if (spa_json_next(json, &val) < 0)
			break;
	}
	def_node = spa_list_first(&graph->node_list, struct node, link);
	if ((out_port = find_port(def_node, output, LADSPA_PORT_OUTPUT)) == NULL) {
		pw_log_error("unknown output port %s", output);
		return -ENOENT;
	}
	def_node = spa_list_last(&graph->node_list, struct node, link);
	if ((in_port = find_port(def_node, input, LADSPA_PORT_INPUT)) == NULL) {
		pw_log_error("unknown input port %s", input);
		return -ENOENT;
	}
	if (in_port->n_links > 0) {
		pw_log_info("Can't have more than 1 link to %s, use a mixer", input);
		return -ENOTSUP;
	}

	if ((link = calloc(1, sizeof(*link))) == NULL)
		return -errno;

	link->output = out_port;
	link->input = in_port;

	pw_log_info("linking %s:%s -> %s:%s",
			out_port->node->name,
			out_port->node->desc->desc->PortNames[out_port->p],
			in_port->node->name,
			in_port->node->desc->desc->PortNames[in_port->p]);

	spa_list_append(&out_port->link_list, &link->output_link);
	out_port->n_links++;
	spa_list_append(&in_port->link_list, &link->input_link);
	in_port->n_links++;

	in_port->node->n_deps++;

	spa_list_append(&graph->link_list, &link->link);

	return 0;
}

static void link_free(struct link *link)
{
	spa_list_remove(&link->input_link);
	link->input->n_links--;
	link->input->node->n_deps--;
	spa_list_remove(&link->output_link);
	link->output->n_links--;
	spa_list_remove(&link->link);
	free(link);
}

/**
 * type = ladspa
 * name = rev
 * plugin = g2reverb
 * label = G2reverb
 * control = [
 *     ...
 * ]
 */
static int load_node(struct graph *graph, struct spa_json *json)
{
	struct spa_json it[1];
	struct ladspa_descriptor *desc;
	struct node *node;
	const char *val;
	char key[256];
	char type[256] = "";
	char name[256] = "";
	char plugin[256] = "";
	char label[256] = "";
	bool have_control = false;
	uint32_t i;

	while (spa_json_get_string(json, key, sizeof(key)) > 0) {
		if (spa_streq("type", key)) {
			if (spa_json_get_string(json, type, sizeof(type)) <= 0) {
				pw_log_error("type expects a string");
				return -EINVAL;
			}
		} else if (spa_streq("name", key)) {
			if (spa_json_get_string(json, name, sizeof(name)) <= 0) {
				pw_log_error("name expects a string");
				return -EINVAL;
			}
		} else if (spa_streq("plugin", key)) {
			if (spa_json_get_string(json, plugin, sizeof(plugin)) <= 0) {
				pw_log_error("plugin expects a string");
				return -EINVAL;
			}
		} else if (spa_streq("label", key)) {
			if (spa_json_get_string(json, label, sizeof(label)) <= 0) {
				pw_log_error("label expects a string");
				return -EINVAL;
			}
		} else if (spa_streq("control", key)) {
			it[0] = *json;
			have_control = true;
		} else if (spa_json_next(json, &val) < 0)
			break;
	}

	if (spa_streq(type, "builtin")) {
		snprintf(plugin, sizeof(plugin), "%s", "builtin");
	} else if (!spa_streq(type, "ladspa"))
		return -ENOTSUP;

	pw_log_info("loading %s %s", plugin, label);
	if ((desc = ladspa_descriptor_load(graph->impl, plugin, label)) == NULL)
		return -errno;

	node = calloc(1, sizeof(*node));
	if (node == NULL)
		return -errno;

	node->graph = graph;
	node->desc = desc;
	snprintf(node->name, sizeof(node->name), "%s", name);

	for (i = 0; i < desc->n_input; i++) {
		struct port *port = &node->input_port[i];
		port->node = node;
		port->idx = i;
		port->external = SPA_ID_INVALID;
		port->p = desc->input[i];
		spa_list_init(&port->link_list);
	}
	for (i = 0; i < desc->n_output; i++) {
		struct port *port = &node->output_port[i];
		port->node = node;
		port->idx = i;
		port->external = SPA_ID_INVALID;
		port->p = desc->output[i];
		spa_list_init(&port->link_list);
	}
	for (i = 0; i < desc->n_control; i++) {
		struct port *port = &node->control_port[i];
		port->node = node;
		port->idx = i;
		port->external = SPA_ID_INVALID;
		port->p = desc->control[i];
		spa_list_init(&port->link_list);
		port->control_data = desc->default_control[i];
	}
	for (i = 0; i < desc->n_notify; i++) {
		struct port *port = &node->notify_port[i];
		port->node = node;
		port->idx = i;
		port->external = SPA_ID_INVALID;
		port->p = desc->notify[i];
		spa_list_init(&port->link_list);
	}
	if (have_control)
		parse_control(node, &it[0]);

	spa_list_append(&graph->node_list, &node->link);

	return 0;
}

static void node_free(struct node *node)
{
	uint32_t i, j;
	const LADSPA_Descriptor *d = node->desc->desc;

	spa_list_remove(&node->link);
	for (i = 0; i < node->n_hndl; i++) {
		for (j = 0; j < node->desc->n_output; j++)
			free(node->output_port[j].audio_data[i]);
		if (node->hndl[i] == NULL)
			continue;
		if (d->deactivate)
			d->deactivate(node->hndl[i]);
		d->cleanup(node->hndl[i]);
	}
	ladspa_descriptor_unref(node->desc);
	free(node);
}

static struct node *find_next_node(struct graph *graph)
{
	struct node *node;
	spa_list_for_each(node, &graph->node_list, link) {
		if (node->n_deps == 0 && !node->visited) {
			node->visited = true;
			return node;
		}
	}
	return NULL;
}

static int setup_input_port(struct graph *graph, struct port *port)
{
	struct ladspa_descriptor *desc = port->node->desc;
	const LADSPA_Descriptor *d = desc->desc;
	struct link *link;
	uint32_t i, n_hndl = port->node->n_hndl;

	spa_list_for_each(link, &port->link_list, input_link) {
		struct port *peer = link->output;
		for (i = 0; i < n_hndl; i++) {
			pw_log_info("connect input port %s[%d]:%s %p",
					port->node->name, i, d->PortNames[port->p],
					peer->audio_data[i]);
			d->connect_port(port->node->hndl[i], port->p, peer->audio_data[i]);
		}
	}
	return 0;
}

static int setup_output_port(struct graph *graph, struct port *port)
{
	struct ladspa_descriptor *desc = port->node->desc;
	const LADSPA_Descriptor *d = desc->desc;
	struct link *link;
	uint32_t i, n_hndl = port->node->n_hndl;

	spa_list_for_each(link, &port->link_list, output_link) {
		for (i = 0; i < n_hndl; i++) {
			float *data;
			if ((data = port->audio_data[i]) == NULL) {
				data = calloc(1, MAX_SAMPLES * sizeof(float));
				if (data == NULL)
					return -errno;
			}
			port->audio_data[i] = data;
			pw_log_info("connect output port %s[%d]:%s %p",
					port->node->name, i, d->PortNames[port->p],
					port->audio_data[i]);
			d->connect_port(port->node->hndl[i], port->p, data);
		}
		link->input->node->n_deps--;
	}
	return 0;
}

static int setup_graph(struct graph *graph, struct spa_json *inputs, struct spa_json *outputs)
{
	struct impl *impl = graph->impl;
	struct node *node, *first, *last;
	struct port *port;
	struct graph_port *gp;
	struct graph_hndl *gh;
	uint32_t i, j, n_input, n_output, n_hndl = 0;
	int res;
	unsigned long p;
	struct ladspa_descriptor *desc;
	const LADSPA_Descriptor *d;
	char v[256];

	graph->n_input = 0;
	graph->n_output = 0;

	first = spa_list_first(&graph->node_list, struct node, link);
	last = spa_list_last(&graph->node_list, struct node, link);

	/* calculate the number of inputs and outputs into the graph.
	 * If we have a list of inputs/outputs, just count them. Otherwise
	 * we count all input ports of the first node and all output
	 * ports of the last node */
	if (inputs != NULL) {
		n_input = count_array(inputs);
	} else {
		n_input = first->desc->n_input;
	}
	if (outputs != NULL) {
		n_output = count_array(outputs);
	} else {
		n_output = last->desc->n_output;
	}
	if (n_input == 0) {
		pw_log_error("no inputs");
		res = -EINVAL;
		goto error;
	}
	if (n_output == 0) {
		pw_log_error("no outputs");
		res = -EINVAL;
		goto error;
	}

	if (impl->capture_info.channels == 0)
		impl->capture_info.channels = n_input;
	if (impl->playback_info.channels == 0)
		impl->playback_info.channels = n_output;

	/* compare to the requested number of channels and duplicate the
	 * graph m_hndl times when needed. */
	n_hndl = impl->capture_info.channels / n_input;
	if (n_hndl != impl->playback_info.channels / n_output) {
		pw_log_error("invalid channels");
		res = -EINVAL;
		goto error;
	}
	pw_log_info("using %d instances %d %d", n_hndl, n_input, n_output);

	/* now go over all nodes and create instances. We can also link
	 * the control and notify ports already */
	graph->n_control = 0;
	spa_list_for_each(node, &graph->node_list, link) {
		desc = node->desc;
		d = desc->desc;
		for (i = 0; i < n_hndl; i++) {
			if ((node->hndl[i] = d->instantiate(d, impl->rate)) == NULL) {
				pw_log_error("cannot create plugin instance");
				res = -ENOMEM;
				goto error;
			}
			node->n_hndl = i + 1;

			for (j = 0; j < desc->n_input; j++) {
				p = desc->input[j];
				d->connect_port(node->hndl[i], p, graph->silence_data);
			}
			for (j = 0; j < desc->n_output; j++) {
				p = desc->output[j];
				d->connect_port(node->hndl[i], p, graph->discard_data);
			}
			for (j = 0; j < desc->n_control; j++) {
				port = &node->control_port[j];
				d->connect_port(node->hndl[i], port->p, &port->control_data);
			}
			for (j = 0; j < desc->n_notify; j++) {
				port = &node->notify_port[j];
				d->connect_port(node->hndl[i], port->p, &port->control_data);
			}
			if (d->activate)
				d->activate(node->hndl[i]);

		}
		/* collect all control ports on the graph */
		for (j = 0; j < desc->n_control; j++) {
			graph->control_port[graph->n_control] = &node->control_port[j];
			graph->n_control++;
		}
	}
	/* now collect all input and output ports for all the handles. */
	for (i = 0; i < n_hndl; i++) {
		if (inputs == NULL) {
			desc = first->desc;
			d = desc->desc;
			for (j = 0; j < desc->n_input; j++) {
				gp = &graph->input[graph->n_input++];
				pw_log_info("input port %s[%d]:%s",
						first->name, i, d->PortNames[desc->input[j]]);
				gp->desc = d;
				gp->hndl = first->hndl[i];
				gp->port = desc->input[j];
			}
		} else {
			struct spa_json it = *inputs;
			while (spa_json_get_string(&it, v, sizeof(v)) > 0) {
				gp = &graph->input[graph->n_input];
				if (spa_streq(v, "null")) {
					gp->desc = NULL;
					pw_log_info("ignore input port %d", graph->n_input);
				} else if ((port = find_port(first, v, LADSPA_PORT_INPUT)) == NULL) {
					res = -ENOENT;
					pw_log_error("input port %s not found", v);
					goto error;
				} else {
					desc = port->node->desc;
					d = desc->desc;
					if (i == 0 && port->external != SPA_ID_INVALID) {
						pw_log_error("input port %s[%d]:%s already used as input %d, use mixer",
							port->node->name, i, d->PortNames[port->p],
							port->external);
						res = -EBUSY;
						goto error;
					}
					if (port->n_links > 0) {
						pw_log_error("input port %s[%d]:%s already used by link, use mixer",
							port->node->name, i, d->PortNames[port->p]);
						res = -EBUSY;
						goto error;
					}
					pw_log_info("input port %s[%d]:%s",
							port->node->name, i, d->PortNames[port->p]);
					port->external = graph->n_input;
					gp->desc = d;
					gp->hndl = port->node->hndl[i];
					gp->port = port->p;
				}
				graph->n_input++;
			}
		}
		if (outputs == NULL) {
			desc = last->desc;
			d = desc->desc;
			for (j = 0; j < desc->n_output; j++) {
				gp = &graph->output[graph->n_output++];
				pw_log_info("output port %s[%d]:%s",
						last->name, i, d->PortNames[desc->output[j]]);
				gp->desc = d;
				gp->hndl = last->hndl[i];
				gp->port = desc->output[j];
			}
		} else {
			struct spa_json it = *outputs;
			while (spa_json_get_string(&it, v, sizeof(v)) > 0) {
				gp = &graph->output[graph->n_output];
				if (spa_streq(v, "null")) {
					gp->desc = NULL;
					pw_log_info("silence output port %d", graph->n_output);
				} else if ((port = find_port(last, v, LADSPA_PORT_OUTPUT)) == NULL) {
					res = -ENOENT;
					pw_log_error("output port %s not found", v);
					goto error;
				} else {
					desc = port->node->desc;
					d = desc->desc;
					if (i == 0 && port->external != SPA_ID_INVALID) {
						pw_log_error("output port %s[%d]:%s already used as output %d, use copy",
							port->node->name, i, d->PortNames[port->p],
							port->external);
						res = -EBUSY;
						goto error;
					}
					if (port->n_links > 0) {
						pw_log_error("output port %s[%d]:%s already used by link, use copy",
							port->node->name, i, d->PortNames[port->p]);
						res = -EBUSY;
						goto error;
					}
					pw_log_info("output port %s[%d]:%s",
							port->node->name, i, d->PortNames[port->p]);
					port->external = graph->n_output;
					gp->desc = d;
					gp->hndl = port->node->hndl[i];
					gp->port = port->p;
				}
				graph->n_output++;
			}
		}
	}

	/* order all nodes based on dependencies */
	graph->n_hndl = 0;
	while (true) {
		if ((node = find_next_node(graph)) == NULL)
			break;

		desc = node->desc;
		d = desc->desc;

		for (i = 0; i < desc->n_input; i++)
			setup_input_port(graph, &node->input_port[i]);

		for (i = 0; i < n_hndl; i++) {
			gh = &graph->hndl[graph->n_hndl++];
			gh->hndl = node->hndl[i];
			gh->desc = d;
		}

		for (i = 0; i < desc->n_output; i++)
			setup_output_port(graph, &node->output_port[i]);
	}
	return 0;

error:
	spa_list_for_each(node, &graph->node_list, link) {
		for (i = 0; i < node->n_hndl; i++) {
			if (node->hndl[i] != NULL)
				node->desc->desc->cleanup(node->hndl[i]);
			node->hndl[i] = NULL;
		}
		node->n_hndl = 0;
	}
	return res;
}

/**
 * filter.graph = {
 *     nodes = [
 *         { ... } ...
 *     ]
 *     links = [
 *         { ... } ...
 *     ]
 *     inputs = [ ]
 *     outputs = [ ]
 * }
 */
static int load_graph(struct graph *graph, struct pw_properties *props)
{
	struct spa_json it[4];
	struct spa_json inputs, outputs, *pinputs = NULL, *poutputs = NULL;
	const char *json, *val;
	char key[256];
	int res;

	spa_list_init(&graph->node_list);
	spa_list_init(&graph->link_list);

	if ((json = pw_properties_get(props, "filter.graph")) == NULL) {
		pw_log_error("missing filter.graph property");
		return -EINVAL;
	}

	spa_json_init(&it[0], json, strlen(json));
        if (spa_json_enter_object(&it[0], &it[1]) <= 0) {
		pw_log_error("filter.graph must be an object");
		return -EINVAL;
	}

	while (spa_json_get_string(&it[1], key, sizeof(key)) > 0) {
		if (spa_streq("nodes", key)) {
			if (spa_json_enter_array(&it[1], &it[2]) <= 0) {
				pw_log_error("nodes expect an array");
				return -EINVAL;
			}
			while (spa_json_enter_object(&it[2], &it[3]) > 0) {
				if ((res = load_node(graph, &it[3])) < 0)
					return res;
			}
		}
		else if (spa_streq("links", key)) {
			if (spa_json_enter_array(&it[1], &it[2]) <= 0)
				return -EINVAL;

			while (spa_json_enter_object(&it[2], &it[3]) > 0) {
				if ((res = parse_link(graph, &it[3])) < 0)
					return res;
			}
		}
		else if (spa_streq("inputs", key)) {
			if (spa_json_enter_array(&it[1], &inputs) <= 0)
				return -EINVAL;
			pinputs = &inputs;
		}
		else if (spa_streq("outputs", key)) {
			if (spa_json_enter_array(&it[1], &outputs) <= 0)
				return -EINVAL;
			poutputs = &outputs;
		} else if (spa_json_next(&it[1], &val) < 0)
			break;
	}
	return setup_graph(graph, pinputs, poutputs);
}

static void graph_free(struct graph *graph)
{
	struct link *link;
	struct node *node;
	spa_list_consume(link, &graph->link_list, link)
		link_free(link);
	spa_list_consume(node, &graph->node_list, link)
		node_free(node);
}

static void core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	struct impl *impl = data;

	pw_log_error("error id:%u seq:%d res:%d (%s): %s",
			id, seq, res, spa_strerror(res), message);

	if (id == PW_ID_CORE && res == -EPIPE)
		unload_module(impl);
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.error = core_error,
};

static void core_destroy(void *d)
{
	struct impl *impl = d;
	spa_hook_remove(&impl->core_listener);
	impl->core = NULL;
	unload_module(impl);
}

static const struct pw_proxy_events core_proxy_events = {
	.destroy = core_destroy,
};

static void impl_destroy(struct impl *impl)
{
	if (impl->capture)
		pw_stream_destroy(impl->capture);
	if (impl->playback)
		pw_stream_destroy(impl->playback);
	if (impl->core && impl->do_disconnect)
		pw_core_disconnect(impl->core);
	pw_properties_free(impl->capture_props);
	pw_properties_free(impl->playback_props);
	if (impl->work)
		pw_work_queue_cancel(impl->work, impl, SPA_ID_INVALID);
	graph_free(&impl->graph);
	free(impl);
}

static void module_destroy(void *data)
{
	struct impl *impl = data;
	impl->unloading = true;
	spa_hook_remove(&impl->module_listener);
	impl_destroy(impl);
}

static const struct pw_impl_module_events module_events = {
	PW_VERSION_IMPL_MODULE_EVENTS,
	.destroy = module_destroy,
};

static uint32_t channel_from_name(const char *name)
{
	int i;
	for (i = 0; spa_type_audio_channel[i].name; i++) {
		if (spa_streq(name, spa_debug_type_short_name(spa_type_audio_channel[i].name)))
			return spa_type_audio_channel[i].type;
	}
	return SPA_AUDIO_CHANNEL_UNKNOWN;
}

static void parse_position(struct spa_audio_info_raw *info, const char *val, size_t len)
{
	struct spa_json it[2];
	char v[256];

	spa_json_init(&it[0], val, len);
        if (spa_json_enter_array(&it[0], &it[1]) <= 0)
                spa_json_init(&it[1], val, len);

	info->channels = 0;
	while (spa_json_get_string(&it[1], v, sizeof(v)) > 0 &&
	    info->channels < SPA_AUDIO_MAX_CHANNELS) {
		info->position[info->channels++] = channel_from_name(v);
	}
}

static void parse_audio_info(struct pw_properties *props, struct spa_audio_info_raw *info)
{
	const char *str;

	*info = SPA_AUDIO_INFO_RAW_INIT(
			.format = SPA_AUDIO_FORMAT_F32P);
	if ((str = pw_properties_get(props, PW_KEY_AUDIO_RATE)) != NULL)
		info->rate = atoi(str);
	if ((str = pw_properties_get(props, PW_KEY_AUDIO_CHANNELS)) != NULL)
		info->channels = atoi(str);
	if ((str = pw_properties_get(props, SPA_KEY_AUDIO_POSITION)) != NULL)
		parse_position(info, str, strlen(str));
}

static void copy_props(struct impl *impl, struct pw_properties *props, const char *key)
{
	const char *str;
	if ((str = pw_properties_get(props, key)) != NULL) {
		if (pw_properties_get(impl->capture_props, key) == NULL)
			pw_properties_set(impl->capture_props, key, str);
		if (pw_properties_get(impl->playback_props, key) == NULL)
			pw_properties_set(impl->playback_props, key, str);
	}
}

SPA_EXPORT
int pipewire__module_init(struct pw_impl_module *module, const char *args)
{
	struct pw_context *context = pw_impl_module_get_context(module);
	struct pw_properties *props;
	struct impl *impl;
	uint32_t id = pw_global_get_id(pw_impl_module_get_global(module));
	const char *str;
	int res;

	impl = calloc(1, sizeof(struct impl));
	if (impl == NULL)
		return -errno;

	pw_log_debug("module %p: new %s", impl, args);

	if (args)
		props = pw_properties_new_string(args);
	else
		props = pw_properties_new(NULL, NULL);

	if (props == NULL) {
		res = -errno;
		pw_log_error( "can't create properties: %m");
		goto error;
	}

	impl->capture_props = pw_properties_new(NULL, NULL);
	impl->playback_props = pw_properties_new(NULL, NULL);
	if (impl->capture_props == NULL || impl->playback_props == NULL) {
		res = -errno;
		pw_log_error( "can't create properties: %m");
		goto error;
	}

	impl->module = module;
	impl->context = context;
	impl->work = pw_context_get_work_queue(context);
	if (impl->work == NULL) {
		res = -errno;
		pw_log_error( "can't create work queue: %m");
		goto error;
	}
	impl->rate = 48000;
	impl->graph.impl = impl;
	spa_list_init(&impl->ladspa_handle_list);

	if (pw_properties_get(props, PW_KEY_NODE_GROUP) == NULL)
		pw_properties_setf(props, PW_KEY_NODE_GROUP, "filter-chain-%u", id);
	if (pw_properties_get(props, PW_KEY_NODE_LINK_GROUP) == NULL)
		pw_properties_setf(props, PW_KEY_NODE_LINK_GROUP, "filter-chain-%u", id);
	if (pw_properties_get(props, PW_KEY_NODE_VIRTUAL) == NULL)
		pw_properties_set(props, PW_KEY_NODE_VIRTUAL, "true");

	if ((str = pw_properties_get(props, "capture.props")) != NULL)
		pw_properties_update_string(impl->capture_props, str, strlen(str));
	if ((str = pw_properties_get(props, "playback.props")) != NULL)
		pw_properties_update_string(impl->playback_props, str, strlen(str));

	copy_props(impl, props, PW_KEY_AUDIO_RATE);
	copy_props(impl, props, PW_KEY_AUDIO_CHANNELS);
	copy_props(impl, props, SPA_KEY_AUDIO_POSITION);
	copy_props(impl, props, PW_KEY_NODE_NAME);
	copy_props(impl, props, PW_KEY_NODE_DESCRIPTION);
	copy_props(impl, props, PW_KEY_NODE_GROUP);
	copy_props(impl, props, PW_KEY_NODE_LINK_GROUP);
	copy_props(impl, props, PW_KEY_NODE_LATENCY);
	copy_props(impl, props, PW_KEY_NODE_VIRTUAL);
	copy_props(impl, props, PW_KEY_MEDIA_NAME);

	parse_audio_info(impl->capture_props, &impl->capture_info);
	parse_audio_info(impl->playback_props, &impl->playback_info);

	if ((res = load_graph(&impl->graph, props)) < 0) {
		pw_log_error("can't load graph: %s", spa_strerror(res));
		goto error;
	}

	if (pw_properties_get(impl->capture_props, PW_KEY_MEDIA_NAME) == NULL)
		pw_properties_setf(impl->capture_props, PW_KEY_MEDIA_NAME, "filter input %u",
				id);
	if (pw_properties_get(impl->playback_props, PW_KEY_MEDIA_NAME) == NULL)
		pw_properties_setf(impl->playback_props, PW_KEY_MEDIA_NAME, "filter output %u",
				id);

	impl->core = pw_context_get_object(impl->context, PW_TYPE_INTERFACE_Core);
	if (impl->core == NULL) {
		str = pw_properties_get(props, PW_KEY_REMOTE_NAME);
		impl->core = pw_context_connect(impl->context,
				pw_properties_new(
					PW_KEY_REMOTE_NAME, str,
					NULL),
				0);
		impl->do_disconnect = true;
	}
	if (impl->core == NULL) {
		res = -errno;
		pw_log_error("can't connect: %m");
		goto error;
	}

	pw_properties_free(props);

	pw_proxy_add_listener((struct pw_proxy*)impl->core,
			&impl->core_proxy_listener,
			&core_proxy_events, impl);
	pw_core_add_listener(impl->core,
			&impl->core_listener,
			&core_events, impl);

	setup_streams(impl);

	pw_impl_module_add_listener(module, &impl->module_listener, &module_events, impl);

	pw_impl_module_update_properties(module, &SPA_DICT_INIT_ARRAY(module_props));

	return 0;

error:
	pw_properties_free(props);
	impl_destroy(impl);
	return res;
}
