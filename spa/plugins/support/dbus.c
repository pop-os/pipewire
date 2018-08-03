/* PipeWire
 * Copyright (C) 2017 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include <spa/support/type-map.h>
#include <spa/support/log.h>
#include <spa/support/plugin.h>
#include <spa/support/dbus.h>

#define NAME "dbus"

struct type {
	uint32_t dbus;
};

static inline void init_type(struct type *type, struct spa_type_map *map)
{
	type->dbus = spa_type_map_get_id(map, SPA_TYPE__DBus);
}

struct impl {
	struct spa_handle handle;
	struct spa_dbus dbus;

	struct type type;

	struct spa_type_map *map;
	struct spa_log *log;
	struct spa_loop_utils *utils;

	struct spa_list connection_list;
};

struct connection {
	struct spa_list link;

	struct spa_dbus_connection this;
	struct impl *impl;
	DBusConnection *conn;
	struct spa_source *dispatch_event;
};

static void dispatch_cb(void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;

	if (dbus_connection_dispatch(conn->conn) == DBUS_DISPATCH_COMPLETE)
		spa_loop_utils_enable_idle(impl->utils, conn->dispatch_event, false);
}

static void dispatch_status(DBusConnection *conn, DBusDispatchStatus status, void *userdata)
{
	struct connection *c = userdata;
	struct impl *impl = c->impl;

	spa_loop_utils_enable_idle(impl->utils, c->dispatch_event,
			status == DBUS_DISPATCH_COMPLETE ? false : true);
}

static inline enum spa_io dbus_to_io(DBusWatch *watch)
{
	enum spa_io mask;
	unsigned int flags;

	/* no watch flags for disabled watches */
	if (!dbus_watch_get_enabled(watch))
		return 0;

	flags = dbus_watch_get_flags(watch);
	mask = SPA_IO_HUP | SPA_IO_ERR;

	if (flags & DBUS_WATCH_READABLE)
		mask |= SPA_IO_IN;
	if (flags & DBUS_WATCH_WRITABLE)
		mask |= SPA_IO_OUT;

	return mask;
}

static inline unsigned int io_to_dbus(enum spa_io mask)
{
	unsigned int flags = 0;

	if (mask & SPA_IO_IN)
		flags |= DBUS_WATCH_READABLE;
	if (mask & SPA_IO_OUT)
		flags |= DBUS_WATCH_WRITABLE;
	if (mask & SPA_IO_HUP)
		flags |= DBUS_WATCH_HANGUP;
	if (mask & SPA_IO_ERR)
		flags |= DBUS_WATCH_ERROR;
	return flags;
}

static void
handle_io_event(void *userdata, int fd, enum spa_io mask)
{
	DBusWatch *watch = userdata;

	if (!dbus_watch_get_enabled(watch)) {
		fprintf(stderr, "Asked to handle disabled watch: %p %i", (void *) watch, fd);
		return;
	}
	dbus_watch_handle(watch, io_to_dbus(mask));
}

static dbus_bool_t add_watch(DBusWatch *watch, void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;
	struct spa_source *source;

	spa_log_debug(impl->log, "add watch %p %d", watch, dbus_watch_get_unix_fd(watch));

	/* we dup because dbus tends to add the same fd multiple times and our epoll
	 * implementation does not like that */
	source = spa_loop_utils_add_io(impl->utils,
				dup(dbus_watch_get_unix_fd(watch)),
				dbus_to_io(watch), true, handle_io_event, watch);

	dbus_watch_set_data(watch, source, NULL);
	return TRUE;
}

static void remove_watch(DBusWatch *watch, void *userdata)
{
	struct connection *conn = userdata;
	struct spa_source *source;

	if ((source = dbus_watch_get_data(watch)))
		spa_loop_utils_destroy_source(conn->impl->utils, source);
}

static void toggle_watch(DBusWatch *watch, void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;
	struct spa_source *source;

	source = dbus_watch_get_data(watch);

	spa_loop_utils_update_io(impl->utils, source, dbus_to_io(watch));
}

struct timeout_data {
	struct spa_source *source;
	struct connection *conn;
};

static void
handle_timer_event(void *userdata, uint64_t expirations)
{
	DBusTimeout *timeout = userdata;
	uint64_t t;
	struct timespec ts;
	struct timeout_data *data = dbus_timeout_get_data(timeout);
	struct connection *conn = data->conn;
	struct impl *impl = conn->impl;

	if (dbus_timeout_get_enabled(timeout)) {
		t = dbus_timeout_get_interval(timeout) * SPA_NSEC_PER_MSEC;
		ts.tv_sec = t / SPA_NSEC_PER_SEC;
		ts.tv_nsec = t % SPA_NSEC_PER_SEC;
		spa_loop_utils_update_timer(impl->utils,
				     data->source, &ts, NULL, false);
		dbus_timeout_handle(timeout);
	}
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;
	struct timespec ts;
	struct timeout_data *data;
	uint64_t t;

	if (!dbus_timeout_get_enabled(timeout))
		return FALSE;

	data = calloc(1, sizeof(struct timeout_data));
	data->conn = conn;
	data->source = spa_loop_utils_add_timer(impl->utils, handle_timer_event, timeout);
	dbus_timeout_set_data(timeout, data, NULL);

	t = dbus_timeout_get_interval(timeout) * SPA_NSEC_PER_MSEC;
	ts.tv_sec = t / SPA_NSEC_PER_SEC;
	ts.tv_nsec = t % SPA_NSEC_PER_SEC;
	spa_loop_utils_update_timer(impl->utils, data->source, &ts, NULL, false);

	return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;
	struct timeout_data *data;

	if ((data = dbus_timeout_get_data(timeout))) {
		spa_loop_utils_destroy_source(impl->utils, data->source);
		free(data);
	}
}

static void toggle_timeout(DBusTimeout *timeout, void *userdata)
{
	struct connection *conn = userdata;
	struct impl *impl = conn->impl;
	struct timeout_data *data;
	struct timespec ts, *tsp;

	data = dbus_timeout_get_data(timeout);

	if (dbus_timeout_get_enabled(timeout)) {
		uint64_t t = dbus_timeout_get_interval(timeout) * SPA_NSEC_PER_MSEC;
		ts.tv_sec = t / SPA_NSEC_PER_SEC;
		ts.tv_nsec = t % SPA_NSEC_PER_SEC;
		tsp = &ts;
	} else {
		tsp = NULL;
	}
	spa_loop_utils_update_timer(impl->utils, data->source, tsp, NULL, false);
}

static void wakeup_main(void *userdata)
{
	struct connection *this = userdata;
	struct impl *impl = this->impl;

	spa_loop_utils_enable_idle(impl->utils, this->dispatch_event, true);
}

static void *
impl_connection_get(struct spa_dbus_connection *conn)
{
	struct connection *this = SPA_CONTAINER_OF(conn, struct connection, this);
	return this->conn;
}

static void
impl_connection_destroy(struct spa_dbus_connection *conn)
{
	struct connection *this = SPA_CONTAINER_OF(conn, struct connection, this);
	struct impl *impl = this->impl;

	dbus_connection_close(this->conn);
	dbus_connection_unref(this->conn);

	spa_loop_utils_destroy_source(impl->utils, this->dispatch_event);

	spa_list_remove(&this->link);

	free(this);
}

static const struct spa_dbus_connection impl_connection = {
	SPA_VERSION_DBUS_CONNECTION,
	impl_connection_get,
	impl_connection_destroy,
};

static struct spa_dbus_connection *
impl_get_connection(struct spa_dbus *dbus,
		    enum spa_dbus_type type)
{
        struct impl *impl = SPA_CONTAINER_OF(dbus, struct impl, dbus);
	struct connection *conn;
        DBusError error;

	dbus_error_init(&error);

	conn = calloc(1, sizeof(struct connection));
	conn->this = impl_connection;
	conn->impl = impl;
	conn->conn = dbus_bus_get_private(type, &error);
	if (conn->conn == NULL)
		goto error;

	conn->dispatch_event = spa_loop_utils_add_idle(impl->utils,
						false, dispatch_cb, conn);

	dbus_connection_set_exit_on_disconnect(conn->conn, false);
	dbus_connection_set_dispatch_status_function(conn->conn, dispatch_status, conn, NULL);
	dbus_connection_set_watch_functions(conn->conn, add_watch, remove_watch, toggle_watch, conn,
					    NULL);
	dbus_connection_set_timeout_functions(conn->conn, add_timeout, remove_timeout,
					      toggle_timeout, conn, NULL);
	dbus_connection_set_wakeup_main_function(conn->conn, wakeup_main, conn, NULL);

	spa_list_append(&impl->connection_list, &conn->link);

	return &conn->this;

      error:
	spa_log_error(impl->log, "Failed to connect to system bus: %s", error.message);
	dbus_error_free(&error);
	free(conn);
	return NULL;
}

static const struct spa_dbus impl_dbus = {
	SPA_VERSION_DBUS,
	impl_get_connection,
};

static int impl_get_interface(struct spa_handle *handle, uint32_t interface_id, void **interface)
{
	struct impl *this;

	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	this = (struct impl *) handle;

	if (interface_id == this->type.dbus)
		*interface = &this->dbus;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	spa_return_val_if_fail(handle != NULL, -EINVAL);
	return 0;
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	struct impl *this;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	this = (struct impl *) handle;
	spa_list_init(&this->connection_list);

	this->dbus = impl_dbus;

	for (i = 0; i < n_support; i++) {
		if (strcmp(support[i].type, SPA_TYPE__TypeMap) == 0)
			this->map = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE__Log) == 0)
			this->log = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE__LoopUtils) == 0)
			this->utils = support[i].data;
	}
	if (this->map == NULL) {
		spa_log_error(this->log, "a type-map is needed");
		return -EINVAL;
	}
	if (this->utils == NULL) {
		spa_log_error(this->log, "a LoopUtils is needed");
		return -EINVAL;
	}
	init_type(&this->type, this->map);

	spa_log_debug(this->log, NAME " %p: initialized", this);

	return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE__DBus,},
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

static const struct spa_handle_factory dbus_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	NAME,
	NULL,
	sizeof(struct impl),
	impl_init,
	impl_enum_interface_info,
};

int
spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*factory = &dbus_factory;
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}
