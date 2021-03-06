/* PipeWire
 *
 * Copyright © 2020 Wim Taymans
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

#ifndef PULSER_SERVER_OPERATION_H
#define PULSER_SERVER_OPERATION_H

#include <stdint.h>

#include <spa/utils/list.h>

struct client;

struct operation {
	struct spa_list link;
	struct client *client;
	uint32_t tag;
	void (*callback) (void *data, struct client *client, uint32_t tag);
	void *data;
};

int operation_new(struct client *client, uint32_t tag);
int operation_new_cb(struct client *client, uint32_t tag,
		void (*callback) (void *data, struct client *client, uint32_t tag),
		void *data);
struct operation *operation_find(struct client *client, uint32_t tag);
void operation_free(struct operation *o);
void operation_complete(struct operation *o);

#endif /* PULSER_SERVER_OPERATION_H */
