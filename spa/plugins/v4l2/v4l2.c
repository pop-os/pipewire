/* Spa V4l2 support
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

#include <spa/support/plugin.h>
#include <spa/support/log.h>

extern const struct spa_handle_factory spa_v4l2_source_factory;
extern const struct spa_handle_factory spa_v4l2_udev_factory;
extern const struct spa_handle_factory spa_v4l2_device_factory;

struct spa_log_topic log_topic = SPA_LOG_TOPIC(0, "spa.v4l2");
struct spa_log_topic *v4l2_log_topic = &log_topic;

SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory,
			uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*factory = &spa_v4l2_source_factory;
		break;
	case 1:
		*factory = &spa_v4l2_udev_factory;
		break;
	case 2:
		*factory = &spa_v4l2_device_factory;
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}
