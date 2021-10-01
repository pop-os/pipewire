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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <regex.h>

#include "config.h"

#include <spa/utils/json.h>
#include <spa/utils/string.h>

#include <pipewire/pipewire.h>
#include "media-session.h"

PW_LOG_TOPIC_EXTERN(ms_topic);
#define PW_LOG_TOPIC_DEFAULT ms_topic

static bool find_match(struct spa_json *arr, struct pw_properties *props)
{
	struct spa_json match_obj;

	while (spa_json_enter_object(arr, &match_obj) > 0) {
		char key[256], val[1024];
		const char *str, *value;
		int match = 0, fail = 0;
		int len;

		while (spa_json_get_string(&match_obj, key, sizeof(key)-1) > 0) {
			bool success = false;

			if ((len = spa_json_next(&match_obj, &value)) <= 0)
				break;

			str = pw_properties_get(props, key);

			if (spa_json_is_null(value, len)) {
				success = str == NULL;
			} else {
				spa_json_parse_string(value, SPA_MIN(len, 1023), val);
				value = val;
				len = strlen(val);
			}
			if (str != NULL) {
				if (value[0] == '~') {
					regex_t preg;
					if (regcomp(&preg, value+1, REG_EXTENDED | REG_NOSUB) == 0) {
						if (regexec(&preg, str, 0, NULL, 0) == 0)
							success = true;
						regfree(&preg);
					}
				} else if (strncmp(str, value, len) == 0 &&
				    strlen(str) == (size_t)len) {
					success = true;
				}
			}
			if (success) {
				match++;
				pw_log_debug("'%s' match '%s' < > '%.*s'", key, str, len, value);
			}
			else
				fail++;
		}
		if (match > 0 && fail == 0)
			return true;
	}
	return false;
}

int sm_media_session_match_rules(const char *rules, size_t size, struct pw_properties *props)
{
	const char *val;
	struct spa_json actions;
	struct spa_json it_rules; /* the rules = [] array */
	struct spa_json it_rules_obj; /* one object within that array */
	struct spa_json it_element; /* key/value element within that object */

	spa_json_init(&it_rules, rules, size);
	if (spa_json_enter_array(&it_rules, &it_rules_obj) < 0)
		return 0;

	while (spa_json_enter_object(&it_rules_obj, &it_element) > 0) {
		char key[64];
		bool have_match = false, have_actions = false;

		while (spa_json_get_string(&it_element, key, sizeof(key)-1) > 0) {
			if (spa_streq(key, "matches")) {
				struct spa_json it_matches_array;
				if (spa_json_enter_array(&it_element, &it_matches_array) < 0)
					break;

				have_match = find_match(&it_matches_array, props);
			}
			else if (spa_streq(key, "actions")) {
				if (spa_json_enter_object(&it_element, &actions) > 0)
					have_actions = true;
			}
			else if (spa_json_next(&it_element, &val) <= 0)
                                break;
		}
		if (!have_match || !have_actions)
			continue;

		while (spa_json_get_string(&actions, key, sizeof(key)-1) > 0) {
			int len;
			pw_log_debug("action %s", key);
			if (spa_streq(key, "update-props")) {
				if ((len = spa_json_next(&actions, &val)) <= 0)
					continue;
				if (!spa_json_is_object(val, len))
					continue;
				len = spa_json_container_len(&actions, val, len);

				pw_properties_update_string(props, val, len);
			}
			else if (spa_json_next(&actions, &val) <= 0)
				break;
		}
	}
	return 1;
}
