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
#include <dlfcn.h>

#include "config.h"

#include <spa/utils/result.h>

#include <pipewire/impl.h>
#include <extensions/metadata.h>

#define NAME "metadata"

static const struct spa_dict_item module_props[] = {
	{ PW_KEY_MODULE_AUTHOR, "Wim Taymans <wim.taymans@gmail.com>" },
	{ PW_KEY_MODULE_DESCRIPTION, "Allow clients to create metadata store" },
	{ PW_KEY_MODULE_VERSION, PACKAGE_VERSION },
};


void * pw_metadata_new(struct pw_context *context, struct pw_resource *resource,
		   struct pw_properties *properties);

struct pw_proxy *pw_core_metadata_export(struct pw_core *core,
		const char *type, const struct spa_dict *props, void *object, size_t user_data_size);

int pw_protocol_native_ext_metadata_init(struct pw_context *context);

struct factory_data {
	struct pw_impl_factory *this;

	struct pw_impl_module *module;
	struct spa_hook module_listener;

	struct pw_export_type export_metadata;
};

static void *create_object(void *_data,
			   struct pw_resource *resource,
			   const char *type,
			   uint32_t version,
			   struct pw_properties *properties,
			   uint32_t new_id)
{
	struct factory_data *data = _data;
	void *result;
	struct pw_resource *metadata_resource;
	struct pw_impl_client *client = pw_resource_get_client(resource);
	int res;

	metadata_resource = pw_resource_new(client, new_id, PW_PERM_ALL, type, version, 0);
	if (metadata_resource == NULL) {
		res = -errno;
		goto error_resource;
	}

	if (properties)
		pw_properties_setf(properties, PW_KEY_FACTORY_ID, "%d",
				pw_impl_factory_get_info(data->this)->id);

	result = pw_metadata_new(pw_impl_client_get_context(client), metadata_resource, properties);
	if (result == NULL) {
		res = -errno;
		goto error_node;
	}
	return result;

error_resource:
	pw_log_error("can't create resource: %s", spa_strerror(res));
	pw_resource_errorf_id(resource, new_id, res, "can't create resource: %s", spa_strerror(res));
	goto error_exit;
error_node:
	pw_log_error("can't create metadata: %s", spa_strerror(res));
	pw_resource_errorf_id(resource, new_id, res, "can't create metadata: %s", spa_strerror(res));
	goto error_exit_free;

error_exit_free:
	pw_resource_remove(metadata_resource);
error_exit:
	errno = -res;
	return NULL;
}

static const struct pw_impl_factory_implementation impl_factory = {
	PW_VERSION_IMPL_FACTORY_IMPLEMENTATION,
	.create_object = create_object,
};

static void module_destroy(void *data)
{
	struct factory_data *d = data;

	spa_hook_remove(&d->module_listener);

	spa_list_remove(&d->export_metadata.link);

	pw_impl_factory_destroy(d->this);
}

static void module_registered(void *data)
{
	struct factory_data *d = data;
	struct pw_impl_module *module = d->module;
	struct pw_impl_factory *factory = d->this;
	struct spa_dict_item items[1];
	char id[16];
	int res;

	snprintf(id, sizeof(id), "%d", pw_global_get_id(pw_impl_module_get_global(module)));
	items[0] = SPA_DICT_ITEM_INIT(PW_KEY_MODULE_ID, id);
	pw_impl_factory_update_properties(factory, &SPA_DICT_INIT(items, 1));

	if ((res = pw_impl_factory_register(factory, NULL)) < 0) {
		pw_log_error(NAME" %p: can't register factory: %s", factory, spa_strerror(res));
	}
}

static const struct pw_impl_module_events module_events = {
	PW_VERSION_IMPL_MODULE_EVENTS,
	.destroy = module_destroy,
	.registered = module_registered,
};

SPA_EXPORT
int pipewire__module_init(struct pw_impl_module *module, const char *args)
{
	struct pw_context *context = pw_impl_module_get_context(module);
	struct pw_impl_factory *factory;
	struct factory_data *data;
	int res;

	if ((res = pw_protocol_native_ext_metadata_init(context)) < 0)
		return res;

	factory = pw_context_create_factory(context,
				 "metadata",
				 PW_TYPE_INTERFACE_Metadata,
				 PW_VERSION_METADATA,
				 NULL,
				 sizeof(*data));
	if (factory == NULL)
		return -errno;

	data = pw_impl_factory_get_user_data(factory);
	data->this = factory;
	data->module = module;

	pw_log_debug("module %p: new", module);

	pw_impl_factory_set_implementation(factory,
				      &impl_factory,
				      data);

	data->export_metadata.type = PW_TYPE_INTERFACE_Metadata;
	data->export_metadata.func = pw_core_metadata_export;
	pw_context_register_export_type(context, &data->export_metadata);

	pw_impl_module_add_listener(module, &data->module_listener, &module_events, data);

	pw_impl_module_update_properties(module, &SPA_DICT_INIT_ARRAY(module_props));

	return 0;
}
