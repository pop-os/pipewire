/* Device/adapter/kernel quirk table
 *
 * Copyright © 2021 Pauli Virtanen
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
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <regex.h>
#include <sys/utsname.h>

#include <bluetooth/bluetooth.h>

#include <dbus/dbus.h>

#include <spa/support/log.h>
#include <spa/support/loop.h>
#include <spa/support/dbus.h>
#include <spa/support/plugin.h>
#include <spa/monitor/device.h>
#include <spa/monitor/utils.h>
#include <spa/utils/hook.h>
#include <spa/utils/type.h>
#include <spa/utils/keys.h>
#include <spa/utils/names.h>
#include <spa/utils/result.h>
#include <spa/utils/json.h>
#include <spa/utils/string.h>

#include "a2dp-codecs.h"
#include "defs.h"

#define NAME "bluez5-quirks"

struct spa_bt_quirks {
	struct spa_log *log;

	int force_msbc;
	int force_hw_volume;
	int force_sbc_xq;

	char *device_rules;
	char *adapter_rules;
	char *kernel_rules;
};

static enum spa_bt_feature parse_feature(const char *str)
{
	static const struct { const char *key; enum spa_bt_feature value; } feature_keys[] = {
		{ "msbc", SPA_BT_FEATURE_MSBC },
		{ "msbc-alt1", SPA_BT_FEATURE_MSBC_ALT1 },
		{ "msbc-alt1-rtl", SPA_BT_FEATURE_MSBC_ALT1_RTL },
		{ "hw-volume", SPA_BT_FEATURE_HW_VOLUME },
		{ "hw-volume-mic", SPA_BT_FEATURE_HW_VOLUME_MIC },
		{ "sbc-xq", SPA_BT_FEATURE_SBC_XQ },
	};
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(feature_keys); ++i) {
		if (spa_streq(str, feature_keys[i].key))
			return feature_keys[i].value;
	}
	return 0;
}

static int do_match(const char *rules, struct spa_dict *dict, uint32_t *no_features)
{
	struct spa_json rules_json = SPA_JSON_INIT(rules, strlen(rules));
	struct spa_json rules_arr, it[2];

	if (spa_json_enter_array(&rules_json, &rules_arr) <= 0)
		return 1;

	while (spa_json_enter_object(&rules_arr, &it[0]) > 0) {
		char key[256];
		int match = true;
		uint32_t no_features_cur = 0;

		while (spa_json_get_string(&it[0], key, sizeof(key)-1) > 0) {
			char val[4096];
			const char *str, *value;
			int len;
			bool success = false;

			if (spa_streq(key, "no-features")) {
				if (spa_json_enter_array(&it[0], &it[1]) > 0) {
					while (spa_json_get_string(&it[1], val, sizeof(val)-1) > 0)
						no_features_cur |= parse_feature(val);
				}
				continue;
			}

			if ((len = spa_json_next(&it[0], &value)) <= 0)
				break;

			if (spa_json_is_null(value, len)) {
				value = NULL;
			} else {
				spa_json_parse_string(value, SPA_MIN(len, (int)sizeof(val)-1), val);
				value = val;
			}

			str = spa_dict_lookup(dict, key);
			if (value == NULL) {
				success = str == NULL;
			} else if (str != NULL) {
				if (value[0] == '~') {
					regex_t r;
					if (regcomp(&r, value+1, REG_EXTENDED | REG_NOSUB) == 0) {
						if (regexec(&r, str, 0, NULL, 0) == 0)
							success = true;
						regfree(&r);
					}
				} else if (spa_streq(str, value)) {
					success = true;
				}
			}

			if (!success) {
				match = false;
				break;
			}
		}

		if (match) {
			*no_features = no_features_cur;
			return 0;
		}
	}
	return 0;
}

static int parse_force_flag(const struct spa_dict *info, const char *key)
{
	const char *str;
	str = spa_dict_lookup(info, key);
	if (str == NULL)
		return -1;
	else
		return (strcmp(str, "true") == 0 || atoi(str)) ? 1 : 0;
}

struct spa_bt_quirks *spa_bt_quirks_create(const struct spa_dict *info, struct spa_log *log)
{
	struct spa_bt_quirks *this;
	const char *str;

	if (!info) {
		errno = -EINVAL;
		return NULL;
	}

	this = calloc(1, sizeof(struct spa_bt_quirks));
	if (this == NULL)
		return NULL;

	this->log = log;

	this->force_sbc_xq = parse_force_flag(info, "bluez5.enable-sbc-xq");
	this->force_msbc = parse_force_flag(info, "bluez5.enable-msbc");
	this->force_hw_volume = parse_force_flag(info, "bluez5.enable-hw-volume");

	str = spa_dict_lookup(info, "bluez5.features.kernel");
	this->kernel_rules = str ? strdup(str) : NULL;
	str = spa_dict_lookup(info, "bluez5.features.adapter");
	this->adapter_rules = str ? strdup(str) : NULL;
	str = spa_dict_lookup(info, "bluez5.features.device");
	this->device_rules = str ? strdup(str) : NULL;

	if (!(this->kernel_rules && this->adapter_rules && this->device_rules))
		spa_log_info(this->log, NAME " failed to find data from bluez-hardware.conf");

	return this;
}

void spa_bt_quirks_destroy(struct spa_bt_quirks *this)
{
	free(this->kernel_rules);
	free(this->adapter_rules);
	free(this->device_rules);
	free(this);
}

static void log_props(struct spa_log *log, const struct spa_dict *dict)
{
	const struct spa_dict_item *item;
	spa_dict_for_each(item, dict)
		spa_log_debug(log, "quirk property %s=%s", item->key, item->value);
}

static void strtolower(char *src, char *dst, int maxsize)
{
	while (maxsize > 1 && *src != '\0') {
		*dst = (*src >= 'A' && *src <= 'Z') ? ('a' + (*src - 'A')) : *src;
		++src;
		++dst;
		--maxsize;
	}
	if (maxsize > 0)
		*dst = '\0';
}

int spa_bt_quirks_get_features(const struct spa_bt_quirks *this,
		const struct spa_bt_adapter *adapter,
		const struct spa_bt_device *device,
		uint32_t *features)
{
	struct spa_dict props;
	struct spa_dict_item items[5];
	int res;

	*features = ~(uint32_t)0;

	/* Kernel */
	if (this->kernel_rules) {
		uint32_t no_features = 0;
		int nitems = 0;
		struct utsname name;
		if ((res = uname(&name)) < 0)
			return res;
		items[nitems++] = SPA_DICT_ITEM_INIT("sysname", name.sysname);
		items[nitems++] = SPA_DICT_ITEM_INIT("release", name.release);
		items[nitems++] = SPA_DICT_ITEM_INIT("version", name.version);
		props = SPA_DICT_INIT(items, nitems);
		log_props(this->log, &props);
		do_match(this->kernel_rules, &props, &no_features);
		spa_log_debug(this->log, NAME ": kernel quirks:%08x", no_features);
		*features &= ~no_features;
	}

	/* Adapter */
	if (this->adapter_rules) {
		uint32_t no_features = 0;
		int nitems = 0;
		char vendor_id[64], product_id[64], address[64];

		if (spa_bt_format_vendor_product_id(
				adapter->source_id, adapter->vendor_id, adapter->product_id,
				vendor_id, sizeof(vendor_id), product_id, sizeof(product_id)) == 0) {
			items[nitems++] = SPA_DICT_ITEM_INIT("vendor-id", vendor_id);
			items[nitems++] = SPA_DICT_ITEM_INIT("product-id", product_id);
		}
		items[nitems++] = SPA_DICT_ITEM_INIT("bus-type",
				(adapter->bus_type == BUS_TYPE_USB) ? "usb" : "other");
		if (adapter->address) {
			strtolower(adapter->address, address, sizeof(address));
			items[nitems++] = SPA_DICT_ITEM_INIT("address", address);
		}
		props = SPA_DICT_INIT(items, nitems);
		log_props(this->log, &props);
		do_match(this->adapter_rules, &props, &no_features);
		spa_log_debug(this->log, NAME ": adapter quirks:%08x", no_features);
		*features &= ~no_features;
	}

	/* Device */
	if (this->device_rules) {
		uint32_t no_features = 0;
		int nitems = 0;
		char vendor_id[64], product_id[64], version_id[64], address[64];
		if (spa_bt_format_vendor_product_id(
				device->source_id, device->vendor_id, device->product_id,
				vendor_id, sizeof(vendor_id), product_id, sizeof(product_id)) == 0) {
			snprintf(version_id, sizeof(version_id), "%04x",
					(unsigned int)device->version_id);
			items[nitems++] = SPA_DICT_ITEM_INIT("vendor-id", vendor_id);
			items[nitems++] = SPA_DICT_ITEM_INIT("product-id", product_id);
			items[nitems++] = SPA_DICT_ITEM_INIT("version-id", version_id);
		}
		if (device->name)
			items[nitems++] = SPA_DICT_ITEM_INIT("name", device->name);
		if (device->address) {
			strtolower(device->address, address, sizeof(address));
			items[nitems++] = SPA_DICT_ITEM_INIT("address", address);
		}
		props = SPA_DICT_INIT(items, nitems);
		log_props(this->log, &props);
		do_match(this->device_rules, &props, &no_features);
		spa_log_debug(this->log, NAME ": device quirks:%08x", no_features);
		*features &= ~no_features;
	}

	/* Force flags */
	if (this->force_msbc != -1) {
		SPA_FLAG_UPDATE(*features, SPA_BT_FEATURE_MSBC, this->force_msbc);
		SPA_FLAG_UPDATE(*features, SPA_BT_FEATURE_MSBC_ALT1, this->force_msbc);
		SPA_FLAG_UPDATE(*features, SPA_BT_FEATURE_MSBC_ALT1_RTL, this->force_msbc);
	}

	if (this->force_hw_volume != -1)
		SPA_FLAG_UPDATE(*features, SPA_BT_FEATURE_HW_VOLUME, this->force_hw_volume);

	if (this->force_sbc_xq != -1)
		SPA_FLAG_UPDATE(*features, SPA_BT_FEATURE_SBC_XQ, this->force_sbc_xq);

	return 0;
}
