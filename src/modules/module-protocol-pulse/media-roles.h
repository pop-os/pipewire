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

#ifndef PULSE_SERVER_MEDIA_ROLES_H
#define PULSE_SERVER_MEDIA_ROLES_H

#include <stddef.h>

#include <spa/utils/string.h>

struct str_map {
	const char *pw_str;
	const char *pa_str;
	const struct str_map *child;
};

extern const struct str_map media_role_map[];

static inline const struct str_map *str_map_find(const struct str_map *map, const char *pw, const char *pa)
{
	size_t i;
	for (i = 0; map[i].pw_str; i++)
		if ((pw && spa_streq(map[i].pw_str, pw)) ||
		    (pa && spa_streq(map[i].pa_str, pa)))
			return &map[i];
	return NULL;
}

#endif /* PULSE_SERVER_MEDIA_ROLES_H */
