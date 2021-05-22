/* PipeWire
 *
 * Copyright © 2021 Wim Taymans <wim.taymans@gmail.com>
 * Copyright © 2021 Arun Raghavan <arun@asymptotic.io>
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

#ifndef PIPEWIRE_PULSE_MODULE_REGISTRY_H
#define PIPEWIRE_PULSE_MODULE_REGISTRY_H

#include "../internal.h"

struct module *create_module_ladspa_sink(struct impl *impl, const char *argument);
struct module *create_module_ladspa_source(struct impl *impl, const char *argument);
struct module *create_module_loopback(struct impl *impl, const char *argument);
struct module *create_module_native_protocol_tcp(struct impl *impl, const char *argument);
struct module *create_module_null_sink(struct impl *impl, const char *argument);
struct module *create_module_remap_sink(struct impl *impl, const char *argument);
struct module *create_module_remap_source(struct impl *impl, const char *argument);
struct module *create_module_tunnel_sink(struct impl *impl, const char *argument);
struct module *create_module_tunnel_source(struct impl *impl, const char *argument);
struct module *create_module_simple_protocol_tcp(struct impl *impl, const char *argument);
struct module *create_module_pipe_sink(struct impl *impl, const char *argument);
struct module *create_module_zeroconf_discover(struct impl *impl, const char *argument);

#endif
