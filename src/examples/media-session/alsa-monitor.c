/* PipeWire
 *
 * Copyright © 2019 Wim Taymans
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
#include <math.h>
#include <time.h>

#include "config.h"

#include <alsa/asoundlib.h>

#include <spa/monitor/device.h>
#include <spa/monitor/event.h>
#include <spa/node/node.h>
#include <spa/node/keys.h>
#include <spa/utils/result.h>
#include <spa/utils/hook.h>
#include <spa/utils/names.h>
#include <spa/utils/keys.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/debug/dict.h>
#include <spa/debug/pod.h>
#include <spa/support/dbus.h>

#include <pipewire/pipewire.h>
#include <pipewire/main-loop.h>
#include <extensions/session-manager.h>

#include "media-session.h"

#include "reserve.c"

#define DEFAULT_JACK_SECONDS	1

struct node {
	struct impl *impl;
	enum pw_direction direction;
	struct device *device;
	struct spa_list link;
	uint32_t id;

	struct pw_properties *props;

	struct spa_node *node;

	struct sm_node *snode;
	unsigned int acquired:1;
};

struct device {
	struct impl *impl;
	struct spa_list link;
	uint32_t id;
	uint32_t device_id;

	char *factory_name;

	struct rd_device *reserve;
	struct spa_hook sync_listener;
	int seq;
	int priority;

	int profile;
	int pending_profile;

	struct pw_properties *props;

	struct spa_handle *handle;
	struct spa_device *device;
	struct spa_hook device_listener;

	struct sm_device *sdevice;
	struct spa_hook listener;

	uint32_t n_acquired;

	unsigned int first:1;
	unsigned int appeared:1;
	unsigned int probed:1;
	struct spa_list node_list;
};

struct impl {
	struct sm_media_session *session;
	struct spa_hook session_listener;

	DBusConnection *conn;

	struct spa_handle *handle;

	struct spa_device *monitor;
	struct spa_hook listener;

	struct spa_list device_list;

	struct spa_source *jack_timeout;
	struct pw_proxy *jack_device;

	unsigned int use_acp:1;
};

#undef NAME
#define NAME "alsa-monitor"

static int probe_device(struct device *device);

static struct node *alsa_find_node(struct device *device, uint32_t id)
{
	struct node *node;

	spa_list_for_each(node, &device->node_list, link) {
		if (node->id == id)
			return node;
	}
	return NULL;
}

static void alsa_update_node(struct device *device, struct node *node,
		const struct spa_device_object_info *info)
{
	pw_log_debug("update node %u", node->id);

	if (pw_log_level_enabled(SPA_LOG_LEVEL_DEBUG))
		spa_debug_dict(0, info->props);

	pw_properties_update(node->props, info->props);
}

static int node_acquire(void *data)
{
	struct node *node = data;
	struct device *device = node->device;

	pw_log_debug("acquire %u", node->id);

	if (node->acquired)
		return 0;

	node->acquired = true;

	if (device && device->n_acquired++ == 0 && device->reserve)
		return rd_device_acquire(device->reserve);
	else
		return 0;
}

static int node_release(void *data)
{
	struct node *node = data;
	struct device *device = node->device;

	pw_log_debug("release %u", node->id);

	if (!node->acquired)
		return 0;

	node->acquired = false;

	if (device && --device->n_acquired == 0 && device->reserve)
		rd_device_release(device->reserve);
	return 0;
}

static const struct sm_object_methods node_methods = {
	SM_VERSION_OBJECT_METHODS,
	.acquire = node_acquire,
	.release = node_release,
};

static struct node *alsa_create_node(struct device *device, uint32_t id,
		const struct spa_device_object_info *info)
{
	struct node *node;
	struct impl *impl = device->impl;
	int res;
	const char *dev, *subdev, *stream, *profile, *profile_desc;
	int priority;

	pw_log_debug("new node %u", id);

	if (strcmp(info->type, SPA_TYPE_INTERFACE_Node) != 0) {
		errno = EINVAL;
		return NULL;
	}
	node = calloc(1, sizeof(*node));
	if (node == NULL) {
		res = -errno;
		goto exit;
	}

	node->props = pw_properties_new_dict(info->props);

	pw_properties_setf(node->props, PW_KEY_DEVICE_ID, "%d", device->device_id);

	pw_properties_set(node->props, PW_KEY_FACTORY_NAME, info->factory_name);

	if ((dev = pw_properties_get(node->props, SPA_KEY_API_ALSA_PCM_DEVICE)) == NULL)
		if ((dev = pw_properties_get(node->props, "alsa.device")) == NULL)
			dev = "0";
	if ((subdev = pw_properties_get(node->props, SPA_KEY_API_ALSA_PCM_SUBDEVICE)) == NULL)
		if ((subdev = pw_properties_get(node->props, "alsa.subdevice")) == NULL)
			subdev = "0";
	if ((stream = pw_properties_get(node->props, SPA_KEY_API_ALSA_PCM_STREAM)) == NULL)
		stream = "unknown";
	if ((profile = pw_properties_get(node->props, "device.profile.name")) == NULL)
		profile = "unknown";
	profile_desc = pw_properties_get(node->props, "device.profile.description");

	if (!strcmp(stream, "capture"))
		node->direction = PW_DIRECTION_OUTPUT;
	else
		node->direction = PW_DIRECTION_INPUT;

	if (device->first) {
		if (atol(dev) != 0)
			device->priority -= 256;
		device->first = false;
	}

	priority = device->priority;
	if (node->direction == PW_DIRECTION_OUTPUT)
		priority += 1000;
	priority -= atol(dev) * 16;
	priority -= atol(subdev);

	if (strstr(profile, "analog-") == profile)
		priority += 9;
	else if (strstr(profile, "iec958-") == profile)
		priority += 8;

	if (pw_properties_get(node->props, PW_KEY_PRIORITY_DRIVER) == NULL) {
		pw_properties_setf(node->props, PW_KEY_PRIORITY_DRIVER, "%d", priority);
		pw_properties_setf(node->props, PW_KEY_PRIORITY_SESSION, "%d", priority);
	}

	if (pw_properties_get(node->props, SPA_KEY_MEDIA_CLASS) == NULL) {
		if (node->direction == PW_DIRECTION_OUTPUT)
			pw_properties_setf(node->props, SPA_KEY_MEDIA_CLASS, "Audio/Source");
		else
			pw_properties_setf(node->props, SPA_KEY_MEDIA_CLASS, "Audio/Sink");
	}
	if (pw_properties_get(node->props, PW_KEY_NODE_NICK) == NULL) {
		const char *s;
		s = pw_properties_get(device->props, PW_KEY_DEVICE_NICK);
		if (s == NULL)
			s = pw_properties_get(device->props, SPA_KEY_API_ALSA_CARD_NAME);
		if (s == NULL)
			s = pw_properties_get(device->props, "alsa.card_name");
		pw_properties_set(node->props, PW_KEY_NODE_NICK, s);

	}
	if (pw_properties_get(node->props, SPA_KEY_NODE_NAME) == NULL) {
		const char *devname;
		if ((devname = pw_properties_get(device->props, SPA_KEY_DEVICE_NAME)) == NULL)
			devname = "unnamed-device";
		if (strstr(devname, "alsa_card.") == devname)
			devname += 10;
		pw_properties_setf(node->props, SPA_KEY_NODE_NAME, "%s.%s.%s",
				node->direction == PW_DIRECTION_OUTPUT ?
				"alsa_input" : "alsa_output", devname, profile);
	}
	if (pw_properties_get(node->props, PW_KEY_NODE_DESCRIPTION) == NULL) {
		const char *desc, *name = NULL;

		if ((desc = pw_properties_get(device->props, SPA_KEY_DEVICE_DESCRIPTION)) == NULL)
			desc = "unknown";

		name = pw_properties_get(node->props, SPA_KEY_API_ALSA_PCM_NAME);
		if (name == NULL)
			name = pw_properties_get(node->props, SPA_KEY_API_ALSA_PCM_ID);
		if (name == NULL)
			name = dev;

		if (profile_desc != NULL) {
			pw_properties_setf(node->props, PW_KEY_NODE_DESCRIPTION, "%s %s",
					desc, profile_desc);
		} else if (strcmp(subdev, "0")) {
			pw_properties_setf(node->props, PW_KEY_NODE_DESCRIPTION, "%s (%s %s)",
					desc, name, subdev);
		} else if (strcmp(dev, "0")) {
			pw_properties_setf(node->props, PW_KEY_NODE_DESCRIPTION, "%s (%s)",
					desc, name);
		} else {
			pw_properties_setf(node->props, PW_KEY_NODE_DESCRIPTION, "%s",
					desc);
		}
	}

	node->impl = impl;
	node->device = device;
	node->id = id;
	node->snode = sm_media_session_create_node(impl->session,
				"adapter",
				&node->props->dict);
	if (node->snode == NULL) {
		res = -errno;
		goto clean_node;
	}

	node->snode->obj.methods = SPA_CALLBACKS_INIT(&node_methods, node);

	spa_list_append(&device->node_list, &node->link);

	return node;

clean_node:
	pw_properties_free(node->props);
	free(node);
exit:
	errno = -res;
	return NULL;
}

static void alsa_remove_node(struct device *device, struct node *node)
{
	pw_log_debug("remove node %u", node->id);
	spa_list_remove(&node->link);
	sm_object_destroy(&node->snode->obj);
	pw_properties_free(node->props);
	free(node);
}

static void alsa_device_info(void *data, const struct spa_device_info *info)
{
	struct device *device = data;

	if (pw_log_level_enabled(SPA_LOG_LEVEL_DEBUG))
		spa_debug_dict(0, info->props);

	pw_properties_update(device->props, info->props);
}

static void alsa_device_object_info(void *data, uint32_t id,
                const struct spa_device_object_info *info)
{
	struct device *device = data;
	struct node *node;

	node = alsa_find_node(device, id);

	if (info == NULL) {
		if (node == NULL) {
			pw_log_warn("device %p: unknown node %u", device, id);
			return;
		}
		alsa_remove_node(device, node);
	} else if (node == NULL) {
		alsa_create_node(device, id, info);
	} else {
		alsa_update_node(device, node, info);
	}
}

static void alsa_device_event(void *data, const struct spa_event *event)
{
	struct device *device = data;
	struct node *node;
	uint32_t id, type;
	struct spa_pod *props = NULL;

	if (spa_pod_parse_object(&event->pod,
			SPA_TYPE_EVENT_Device, &type,
			SPA_EVENT_DEVICE_Object, SPA_POD_Int(&id),
			SPA_EVENT_DEVICE_Props, SPA_POD_OPT_Pod(&props)) < 0)
		return;

	if ((node = alsa_find_node(device, id)) == NULL)
		return;

	switch (type) {
	case SPA_DEVICE_EVENT_ObjectConfig:
		if (props)
			pw_node_set_param((struct pw_node*)node->snode->obj.proxy,
				SPA_PARAM_Props, 0, props);
		break;
	default:
		break;
	}
}

static const struct spa_device_events alsa_device_events = {
	SPA_VERSION_DEVICE_EVENTS,
	.info = alsa_device_info,
	.object_info = alsa_device_object_info,
	.event = alsa_device_event,
};

static struct device *alsa_find_device(struct impl *impl, uint32_t id)
{
	struct device *device;

	spa_list_for_each(device, &impl->device_list, link) {
		if (device->id == id)
			return device;
	}
	return NULL;
}

static void alsa_update_device(struct impl *impl, struct device *device,
		const struct spa_device_object_info *info)
{
	pw_log_debug("update device %u", device->id);

	if (pw_log_level_enabled(SPA_LOG_LEVEL_DEBUG))
		spa_debug_dict(0, info->props);

	pw_properties_update(device->props, info->props);
}

static int update_device_props(struct device *device)
{
	struct pw_properties *p = device->props;
	const char *s, *d;
	char temp[32];

	s = pw_properties_get(p, SPA_KEY_DEVICE_NAME);
	if (s == NULL)
		s = pw_properties_get(p, SPA_KEY_DEVICE_BUS_ID);
	if (s == NULL)
		s = pw_properties_get(p, SPA_KEY_DEVICE_BUS_PATH);
	if (s == NULL) {
		snprintf(temp, sizeof(temp), "%d", device->id);
		s = temp;
	}
	pw_properties_setf(p, PW_KEY_DEVICE_NAME, "alsa_card.%s", s);

	if (pw_properties_get(p, PW_KEY_DEVICE_DESCRIPTION) == NULL) {
		d = NULL;

		if ((s = pw_properties_get(p, PW_KEY_DEVICE_FORM_FACTOR)))
			if (strcmp(s, "internal") == 0)
				d = "Built-in Audio";
		if (!d)
			if ((s = pw_properties_get(p, PW_KEY_DEVICE_CLASS)))
				if (strcmp(s, "modem") == 0)
					d = "Modem";
		if (!d)
			d = pw_properties_get(p, PW_KEY_DEVICE_PRODUCT_NAME);

		if (!d)
			d = "Unknown device";

		pw_properties_set(p, PW_KEY_DEVICE_DESCRIPTION, d);
	}

	if (pw_properties_get(p, PW_KEY_DEVICE_NICK) == NULL) {
		s = pw_properties_get(p, SPA_KEY_API_ALSA_CARD_NAME);
		if (s != NULL)
			pw_properties_set(p, PW_KEY_DEVICE_NICK, s);
	}

	if (pw_properties_get(p, PW_KEY_DEVICE_ICON_NAME) == NULL) {
		d = NULL;

		if ((s = pw_properties_get(p, PW_KEY_DEVICE_FORM_FACTOR))) {
			if (strcmp(s, "microphone") == 0)
				d = "audio-input-microphone";
			else if (strcmp(s, "webcam") == 0)
				d = "camera-web";
			else if (strcmp(s, "computer") == 0)
				d = "computer";
			else if (strcmp(s, "handset") == 0)
				d = "phone";
			else if (strcmp(s, "portable") == 0)
				d = "multimedia-player";
			else if (strcmp(s, "tv") == 0)
				d = "video-display";
			else if (strcmp(s, "headset") == 0)
				d = "audio-headset";
			else if (strcmp(s, "headphone") == 0)
				d = "audio-headphones";
			else if (strcmp(s, "speaker") == 0)
				d = "audio-speakers";
			else if (strcmp(s, "hands-free") == 0)
				d = "audio-handsfree";
		}
		if (!d)
			if ((s = pw_properties_get(p, PW_KEY_DEVICE_CLASS)))
				if (strcmp(s, "modem") == 0)
					d = "modem";

		if (!d)
			d = "audio-card";

		s = pw_properties_get(p, PW_KEY_DEVICE_BUS);

		pw_properties_setf(p, PW_KEY_DEVICE_ICON_NAME,
				"%s-analog%s%s", d, s ? "-" : "", s);
	}
	return 1;
}

static void set_profile(struct device *device, int index)
{
	struct impl *impl = device->impl;
	char buf[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));

	if (impl->use_acp)
		return;

	pw_log_debug("%p: set profile %d id:%d", device, index, device->device_id);

	if (device->device_id != 0) {
		device->profile = index;
		spa_device_set_param(device->device,
				SPA_PARAM_Profile, 0,
				spa_pod_builder_add_object(&b,
					SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
					SPA_PARAM_PROFILE_index,   SPA_POD_Int(index)));
	}
}

static void set_jack_profile(struct impl *impl, int index)
{
	char buf[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));

	if (impl->jack_device == NULL)
		return;

	pw_device_set_param((struct pw_device*)impl->jack_device,
			SPA_PARAM_Profile, 0,
			spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
				SPA_PARAM_PROFILE_index,   SPA_POD_Int(index)));
}

static void remove_jack_timeout(struct impl *impl)
{
	struct pw_loop *main_loop = impl->session->loop;

	if (impl->jack_timeout) {
		pw_loop_destroy_source(main_loop, impl->jack_timeout);
		impl->jack_timeout = NULL;
	}
}

static void jack_timeout(void *data, uint64_t expirations)
{
	struct impl *impl = data;
	remove_jack_timeout(impl);
	set_jack_profile(impl, 1);
}

static void add_jack_timeout(struct impl *impl)
{
	struct timespec value;
	struct pw_loop *main_loop = impl->session->loop;

	if (impl->jack_timeout == NULL)
		impl->jack_timeout = pw_loop_add_timer(main_loop, jack_timeout, impl);

	value.tv_sec = DEFAULT_JACK_SECONDS;
	value.tv_nsec = 0;
	pw_loop_update_timer(main_loop, impl->jack_timeout, &value, NULL, false);
}

static void reserve_acquired(void *data, struct rd_device *d)
{
	struct device *device = data;

	pw_log_info("%p: reserve acquired %d", device, device->n_acquired);

	if (!device->probed)
		probe_device(device);

	if (device->n_acquired == 0)
		rd_device_release(device->reserve);
}

static void complete_release(struct device *device)
{
	if (device->reserve)
		rd_device_complete_release(device->reserve, true);
}

static void sync_complete_done(void *data, int seq)
{
	struct device *device = data;

	pw_log_debug("%d %d", device->seq, seq);
	if (seq != device->seq)
		return;

	spa_hook_remove(&device->sync_listener);
	device->seq = 0;

	complete_release(device);
}

static void sync_destroy(void *data)
{
	struct device *device = data;
	if (device->seq != 0)
		sync_complete_done(data, device->seq);
}

static const struct pw_proxy_events sync_complete_release = {
	PW_VERSION_PROXY_EVENTS,
	.destroy = sync_destroy,
	.done = sync_complete_done
};

static void reserve_release(void *data, struct rd_device *d, int forced)
{
	struct device *device = data;

	pw_log_info("%p: reserve release", device);
	if (device->sdevice == NULL || device->sdevice->obj.proxy == NULL) {
		complete_release(device);
		return;
	}

	set_profile(device, 0);

	if (device->seq == 0)
		pw_proxy_add_listener(device->sdevice->obj.proxy,
				&device->sync_listener,
				&sync_complete_release, device);
	device->seq = pw_proxy_sync(device->sdevice->obj.proxy, 0);
}

static void reserve_busy(void *data, struct rd_device *d, const char *name, int32_t prio)
{
	struct device *device = data;
	struct impl *impl = device->impl;

	pw_log_info("%p: reserve busy %s", device, name);
	if (device->sdevice == NULL)
		return ;

	device->sdevice->locked = true;

	if (strcmp(name, "jack") == 0) {
		add_jack_timeout(impl);
	} else {
		remove_jack_timeout(impl);
	}
}

static void reserve_available(void *data, struct rd_device *d, const char *name)
{
	struct device *device = data;
	struct impl *impl = device->impl;

	pw_log_info("%p: reserve available %s", device, name);
	if (device->sdevice == NULL)
		return ;

	device->sdevice->locked = false;

	remove_jack_timeout(impl);
	if (strcmp(name, "jack") == 0) {
		set_jack_profile(impl, 0);
	}

}

static const struct rd_device_callbacks reserve_callbacks = {
	.acquired = reserve_acquired,
	.release = reserve_release,
	.busy = reserve_busy,
	.available = reserve_available,
};

static void device_destroy(void *data)
{
	struct device *device = data;
	struct node *node;

	pw_log_debug("device %p destroy", device);
	spa_list_remove(&device->link);

	spa_list_consume(node, &device->node_list, link)
		alsa_remove_node(device, node);

	if (device->appeared)
		spa_hook_remove(&device->device_listener);
	if (device->seq != 0)
		spa_hook_remove(&device->sync_listener);
	if (device->reserve)
		rd_device_destroy(device->reserve);
}

static void device_free(void *data)
{
	struct device *device = data;
	pw_log_debug("device %p free", device);
	spa_hook_remove(&device->listener);
	free(device->factory_name);
	pw_unload_spa_handle(device->handle);
	pw_properties_free(device->props);
	free(device);
}

static void device_update(void *data)
{
	struct device *device = data;

	pw_log_debug("device %p appeared %d %d", device, device->appeared, device->profile);

	if (!device->appeared) {
		device->device_id = device->sdevice->obj.id;
		device->appeared = true;

		spa_device_add_listener(device->device,
			&device->device_listener,
			&alsa_device_events, device);
		sm_object_sync_update(&device->sdevice->obj);
	}
	if (device->pending_profile != device->profile && !device->sdevice->locked)
		set_profile(device, device->pending_profile);
}

static const struct sm_object_events device_events = {
	SM_VERSION_OBJECT_EVENTS,
	.destroy = device_destroy,
	.free = device_free,
	.update = device_update,
};

static int probe_device(struct device *device)
{
	struct impl *impl = device->impl;
	struct pw_context *context = impl->session->context;
	struct spa_handle *handle;
	void *iface;
	int res;

	handle = pw_context_load_spa_handle(context,
			device->factory_name, &device->props->dict);
	if (handle == NULL) {
		res = -errno;
		pw_log_error("can't make factory instance: %m");
		goto exit;
	}

	if ((res = spa_handle_get_interface(handle, SPA_TYPE_INTERFACE_Device, &iface)) < 0) {
		pw_log_error("can't get %s interface: %s", SPA_TYPE_INTERFACE_Device,
				spa_strerror(res));
		goto unload_handle;
	}

	device->handle = handle;
	device->device = iface;

	device->sdevice = sm_media_session_export_device(impl->session,
			&device->props->dict, device->device);
	if (device->sdevice == NULL) {
		res = -errno;
		goto unload_handle;
	}
	sm_object_add_listener(&device->sdevice->obj,
			&device->listener,
			&device_events, device);

	device->probed = true;

	return 0;

unload_handle:
	pw_unload_spa_handle(handle);
exit:
	return res;
}

static struct device *alsa_create_device(struct impl *impl, uint32_t id,
		const struct spa_device_object_info *info)
{
	struct device *device;
	int res;
	const char *str, *card, *factory_name, *name;

	pw_log_debug("new device %u", id);

	if (strcmp(info->type, SPA_TYPE_INTERFACE_Device) != 0) {
		errno = EINVAL;
		return NULL;
	}

	device = calloc(1, sizeof(*device));
	if (device == NULL) {
		res = -errno;
		goto exit;
	}

	if (impl->use_acp)
		factory_name = SPA_NAME_API_ALSA_ACP_DEVICE;
	else
		factory_name = info->factory_name;

	device->factory_name = strdup(factory_name);
	device->impl = impl;
	device->id = id;
	device->props = pw_properties_new_dict(info->props);
	device->priority = 1000;
	device->first = true;
	spa_list_init(&device->node_list);
	update_device_props(device);
	device->pending_profile = 1;
	spa_list_append(&impl->device_list, &device->link);

	if ((str = pw_properties_get(impl->session->props, "alsa.soft-mixer")) != NULL &&
	    (strcmp(str, "*") == 0 ||
	    ((name = pw_properties_get(device->props, "device.name")) != NULL &&
	     strstr(str, name) != NULL))) {
		pw_properties_set(device->props, "api.alsa.soft-mixer", "true");
	}

	if (impl->conn &&
	    (card = spa_dict_lookup(info->props, SPA_KEY_API_ALSA_CARD)) != NULL) {
		const char *reserve;

		device->priority -= atol(card) * 64;

		pw_properties_setf(device->props, "api.dbus.ReserveDevice1", "Audio%s", card);
		reserve = pw_properties_get(device->props, "api.dbus.ReserveDevice1");

		device->reserve = rd_device_new(impl->conn, reserve,
				"PipeWire", -10,
				&reserve_callbacks, device);

		if (device->reserve == NULL) {
			pw_log_warn("can't create device reserve for %s: %m", reserve);
		} else {
			rd_device_set_application_device_name(device->reserve,
				spa_dict_lookup(info->props, SPA_KEY_API_ALSA_PATH));
		}
	}
	if (device->reserve != NULL)
		rd_device_acquire(device->reserve);
	else
		probe_device(device);

	return device;
exit:
	errno = -res;
	return NULL;
}

static void alsa_remove_device(struct impl *impl, struct device *device)
{
	pw_log_debug("%p: remove device %u", device, device->id);
	if (device->sdevice)
		sm_object_destroy(&device->sdevice->obj);
}

static void alsa_udev_object_info(void *data, uint32_t id,
                const struct spa_device_object_info *info)
{
	struct impl *impl = data;
	struct device *device;

	device = alsa_find_device(impl, id);

	if (info == NULL) {
		if (device == NULL)
			return;
		alsa_remove_device(impl, device);
	} else if (device == NULL) {
		if ((device = alsa_create_device(impl, id, info)) == NULL)
			return;
	} else {
		alsa_update_device(impl, device, info);
	}
}

static const struct spa_device_events alsa_udev_events =
{
	SPA_VERSION_DEVICE_EVENTS,
	.object_info = alsa_udev_object_info,
};

static int alsa_start_jack_device(struct impl *impl)
{
	struct pw_properties *props;
	int res = 0;

	props = pw_properties_new(
			SPA_KEY_FACTORY_NAME, SPA_NAME_API_JACK_DEVICE,
			SPA_KEY_NODE_NAME, "JACK-Device",
			NULL);

	impl->jack_device = sm_media_session_create_object(impl->session,
				"spa-device-factory",
				PW_TYPE_INTERFACE_Device,
				PW_VERSION_DEVICE,
				&props->dict,
                                0);

	if (impl->jack_device == NULL)
		res = -errno;

	pw_properties_free(props);

	return res;
}

static void session_destroy(void *data)
{
	struct impl *impl = data;
	remove_jack_timeout(impl);
	spa_hook_remove(&impl->session_listener);
	spa_hook_remove(&impl->listener);
	pw_proxy_destroy(impl->jack_device);
	pw_unload_spa_handle(impl->handle);
	free(impl);
}

static const struct sm_media_session_events session_events = {
	SM_VERSION_MEDIA_SESSION_EVENTS,
	.destroy = session_destroy,
};

int sm_alsa_monitor_start(struct sm_media_session *session)
{
	struct pw_context *context = session->context;
	struct impl *impl;
	void *iface;
	const char *str;
	int res;

	impl = calloc(1, sizeof(struct impl));
	if (impl == NULL)
		return -errno;

	impl->session = session;

	if ((str = pw_properties_get(session->props, "alsa.use-acp")) != NULL)
		impl->use_acp = pw_properties_parse_bool(str);

	if (session->dbus_connection)
		impl->conn = spa_dbus_connection_get(session->dbus_connection);
	if (impl->conn == NULL)
		pw_log_warn("no dbus connection, device reservation disabled");
	else
		pw_log_debug("got dbus connection %p", impl->conn);


	impl->handle = pw_context_load_spa_handle(context, SPA_NAME_API_ALSA_ENUM_UDEV, NULL);
	if (impl->handle == NULL) {
		res = -errno;
		goto out_free;
	}

	if ((res = spa_handle_get_interface(impl->handle, SPA_TYPE_INTERFACE_Device, &iface)) < 0) {
		pw_log_error("can't get udev Device interface: %d", res);
		goto out_unload;
	}
	impl->monitor = iface;
	spa_list_init(&impl->device_list);
	spa_device_add_listener(impl->monitor, &impl->listener, &alsa_udev_events, impl);

	if ((res = alsa_start_jack_device(impl)) < 0)
		goto out_unload;

	sm_media_session_add_listener(session, &impl->session_listener, &session_events, impl);

	return 0;

out_unload:
	pw_unload_spa_handle(impl->handle);
out_free:
	free(impl);
	return res;
}
