/* Spa libcamera support
 *
 * Copyright © 2020 collabora
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

#include <linux/media.h>

#include <spa/support/log.h>

#undef SPA_LOG_TOPIC_DEFAULT
#define SPA_LOG_TOPIC_DEFAULT libcamera_log_topic
extern struct spa_log_topic *libcamera_log_topic;

static inline void libcamera_log_topic_init(struct spa_log *log)
{
	spa_log_topic_init(log, libcamera_log_topic);
}

#include "libcamera_wrapper.h"

struct spa_libcamera_device {
	struct spa_log *log;
	int fd;
	struct media_device_info dev_info;
	unsigned int active:1;
	unsigned int have_format:1;
	LibCamera *camera;
};

int spa_libcamera_open(struct spa_libcamera_device *dev);
int spa_libcamera_close(struct spa_libcamera_device *dev);
int spa_libcamera_is_capture(struct spa_libcamera_device *dev);
int get_dev_fd(struct spa_libcamera_device *dev);
