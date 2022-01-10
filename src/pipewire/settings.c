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

#include "config.h"

#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/monitor/device.h>
#include <spa/monitor/utils.h>
#include <spa/pod/filter.h>
#include <spa/utils/json.h>

#include <pipewire/impl.h>
#include <pipewire/private.h>
#include "pipewire/array.h"
#include "pipewire/core.h"

#include <pipewire/extensions/metadata.h>

#define NAME "settings"

#define DEFAULT_CLOCK_RATE			48000u
#define DEFAULT_CLOCK_QUANTUM			1024u
#define DEFAULT_CLOCK_MIN_QUANTUM		32u
#define DEFAULT_CLOCK_MAX_QUANTUM		8192u
#define DEFAULT_CLOCK_POWER_OF_TWO_QUANTUM	true
#define DEFAULT_VIDEO_WIDTH			640
#define DEFAULT_VIDEO_HEIGHT			480
#define DEFAULT_VIDEO_RATE_NUM			25u
#define DEFAULT_VIDEO_RATE_DENOM		1u
#define DEFAULT_LINK_MAX_BUFFERS		64u
#define DEFAULT_MEM_WARN_MLOCK			false
#define DEFAULT_MEM_ALLOW_MLOCK			true

struct impl {
	struct pw_context *context;
	struct pw_impl_metadata *metadata;

	struct spa_hook metadata_listener;
};

static void metadata_destroy(void *data)
{
	struct impl *impl = data;
	spa_hook_remove(&impl->metadata_listener);
	impl->metadata = NULL;
}

static uint32_t get_default_int(struct pw_properties *properties, const char *name, uint32_t def)
{
	uint32_t val;
	const char *str;
	if ((str = pw_properties_get(properties, name)) != NULL)
		val = atoi(str);
	else {
		val = def;
		pw_properties_setf(properties, name, "%d", val);
	}
	return val;
}

static bool get_default_bool(struct pw_properties *properties, const char *name, bool def)
{
	bool val;
	const char *str;
	if ((str = pw_properties_get(properties, name)) != NULL)
		val = pw_properties_parse_bool(str);
	else {
		val = def;
		pw_properties_set(properties, name, val ? "true" : "false");
	}
	return val;
}

static bool uint32_array_contains(uint32_t *vals, uint32_t n_vals, uint32_t val)
{
	uint32_t i;
	for (i = 0; i < n_vals; i++)
		if (vals[i] == val)
			return true;
	return false;
}

static uint32_t parse_uint32_array(const char *str, uint32_t *vals, uint32_t max, uint32_t def)
{
	uint32_t count = 0, r;
	struct spa_json it[2];
	char v[256];

	spa_json_init(&it[0], str, strlen(str));
	if (spa_json_enter_array(&it[0], &it[1]) <= 0)
		spa_json_init(&it[1], str, strlen(str));

	while (spa_json_get_string(&it[1], v, sizeof(v)) > 0 &&
	    count < max) {
		if (spa_atou32(v, &r, 0))
	                vals[count++] = r;
        }
	if (!uint32_array_contains(vals, count, def))
		count = 0;
	return count;
}

static uint32_t parse_clock_rate(struct pw_properties *properties, const char *name,
		uint32_t *rates, uint32_t def)
{
	const char *str;
	uint32_t count = 0;

	if ((str = pw_properties_get(properties, name)) == NULL)
		return 0;

	count = parse_uint32_array(str, rates, MAX_RATES, def);
	if (count == 0)
		goto fallback;

	return count;
fallback:
	rates[0] = def;
	pw_properties_setf(properties, name, "[ %u ]", def);
	return 1;
}

static int metadata_property(void *data, uint32_t subject, const char *key,
		const char *type, const char *value)
{
	struct impl *impl = data;
	struct pw_context *context = impl->context;
	struct settings *d = &context->defaults;
	struct settings *s = &context->settings;
	uint32_t v;
	bool recalc = false;

	if (subject != PW_ID_CORE)
		return 0;

	if (spa_streq(key, "log.level")) {
		v = value ? atoi(value) : 3;
		pw_log_set_level(v);
	} else if (spa_streq(key, "clock.rate")) {
		v = value ? atoi(value) : 0;
		s->clock_rate = v == 0 ? d->clock_rate : v;
		recalc = true;
	} else if (spa_streq(key, "clock.allowed-rates")) {
		s->n_clock_rates = parse_uint32_array(value,
				s->clock_rates, MAX_RATES, s->clock_rate);
		if (s->n_clock_rates == 0) {
			s->n_clock_rates = d->n_clock_rates;
			memcpy(s->clock_rates, d->clock_rates, MAX_RATES * sizeof(uint32_t));
		}
		recalc = true;
	} else if (spa_streq(key, "clock.quantum")) {
		v = value ? atoi(value) : 0;
		s->clock_quantum = v == 0 ? d->clock_quantum : v;
		recalc = true;
	} else if (spa_streq(key, "clock.min-quantum")) {
		v = value ? atoi(value) : 0;
		s->clock_min_quantum = v == 0 ? d->clock_min_quantum : v;
		recalc = true;
	} else if (spa_streq(key, "clock.max-quantum")) {
		v = value ? atoi(value) : 0;
		s->clock_max_quantum = v == 0 ? d->clock_max_quantum : v;
		recalc = true;
	} else if (spa_streq(key, "clock.force-rate")) {
		v = value ? atoi(value) : 0;
		s->clock_force_rate = v;
		recalc = true;
	} else if (spa_streq(key, "clock.force-quantum")) {
		v = value ? atoi(value) : 0;
		s->clock_force_quantum = SPA_MIN(v, 8192u);
		recalc = true;
	}
	if (recalc)
		pw_context_recalc_graph(context, "settings changed");

	return 0;
}

static const struct pw_impl_metadata_events metadata_events = {
	PW_VERSION_IMPL_METADATA_EVENTS,
	.destroy = metadata_destroy,
	.property = metadata_property,
};

void pw_settings_init(struct pw_context *this)
{
	struct pw_properties *p = this->properties;
	struct settings *d = &this->defaults;

	d->clock_rate = get_default_int(p, "default.clock.rate", DEFAULT_CLOCK_RATE);
	d->n_clock_rates = parse_clock_rate(p, "default.clock.allowed-rates", d->clock_rates, d->clock_rate);
	d->clock_quantum = get_default_int(p, "default.clock.quantum", DEFAULT_CLOCK_QUANTUM);
	d->clock_min_quantum = get_default_int(p, "default.clock.min-quantum", DEFAULT_CLOCK_MIN_QUANTUM);
	d->clock_max_quantum = get_default_int(p, "default.clock.max-quantum", DEFAULT_CLOCK_MAX_QUANTUM);
	d->video_size.width = get_default_int(p, "default.video.width", DEFAULT_VIDEO_WIDTH);
	d->video_size.height = get_default_int(p, "default.video.height", DEFAULT_VIDEO_HEIGHT);
	d->video_rate.num = get_default_int(p, "default.video.rate.num", DEFAULT_VIDEO_RATE_NUM);
	d->video_rate.denom = get_default_int(p, "default.video.rate.denom", DEFAULT_VIDEO_RATE_DENOM);

	d->log_level = get_default_int(p, "log.level", pw_log_level);
	d->clock_power_of_two_quantum = get_default_bool(p, "clock.power-of-two-quantum",
			DEFAULT_CLOCK_POWER_OF_TWO_QUANTUM);
	d->link_max_buffers = get_default_int(p, "link.max-buffers", DEFAULT_LINK_MAX_BUFFERS);
	d->mem_warn_mlock = get_default_bool(p, "mem.warn-mlock", DEFAULT_MEM_WARN_MLOCK);
	d->mem_allow_mlock = get_default_bool(p, "mem.allow-mlock", DEFAULT_MEM_ALLOW_MLOCK);

	d->clock_max_quantum = SPA_CLAMP(d->clock_max_quantum,
			CLOCK_MIN_QUANTUM, CLOCK_MAX_QUANTUM);
	d->clock_min_quantum = SPA_CLAMP(d->clock_min_quantum,
			CLOCK_MIN_QUANTUM, d->clock_max_quantum);
	d->clock_quantum = SPA_CLAMP(d->clock_quantum,
			d->clock_min_quantum, d->clock_max_quantum);
}

static void expose_settings(struct pw_context *context, struct pw_impl_metadata *metadata)
{
	struct settings *s = &context->settings;
	uint32_t i, o;
	char rates[MAX_RATES*16] = "";

	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "log.level", "", "%d", s->log_level);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.rate", "", "%d", s->clock_rate);
	for (i = 0, o = 0; i < s->n_clock_rates; i++) {
		int r = snprintf(rates+o, sizeof(rates)-o, "%s%d", i == 0 ? "" : ", ",
				s->clock_rates[i]);
		if (r < 0 || o + r >= (int)sizeof(rates)) {
			snprintf(rates, sizeof(rates), "%d", s->clock_rate);
			break;
		}
		o += r;
	}
	if (s->n_clock_rates == 0)
		snprintf(rates, sizeof(rates), "%d", s->clock_rate);

	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.allowed-rates", "", "[ %s ]", rates);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.quantum", "", "%d", s->clock_quantum);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.min-quantum", "", "%d", s->clock_min_quantum);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.max-quantum", "", "%d", s->clock_max_quantum);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.force-quantum", "", "%d", s->clock_force_quantum);
	pw_impl_metadata_set_propertyf(metadata,
			PW_ID_CORE, "clock.force-rate", "", "%d", s->clock_force_rate);
}

int pw_settings_expose(struct pw_context *context)
{
	struct impl *impl;

	impl = calloc(1, sizeof(*impl));
	if (impl == NULL)
		return -errno;

	impl->context = context;
	impl->metadata = pw_context_create_metadata(context, "settings", NULL, 0);
	if (impl->metadata == NULL)
		goto error_free;

	expose_settings(context, impl->metadata);

	pw_impl_metadata_add_listener(impl->metadata,
			&impl->metadata_listener,
			&metadata_events, impl);

	pw_impl_metadata_register(impl->metadata, NULL);

	context->settings_impl = impl;

	return 0;

error_free:
	free(impl);
	return -errno;
}

void pw_settings_clean(struct pw_context *context)
{
	struct impl *impl = context->settings_impl;

	if (impl == NULL)
		return;

	context->settings_impl = NULL;
	if (impl->metadata != NULL)
		pw_impl_metadata_destroy(impl->metadata);
	free(impl);
}
