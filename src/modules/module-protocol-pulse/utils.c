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

#define NAME "pulse-server"

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include <spa/utils/result.h>
#include <pipewire/context.h>
#include <pipewire/log.h>
#include <pipewire/keys.h>

#include "utils.h"

int get_runtime_dir(char *buf, size_t buflen, const char *dir)
{
	const char *runtime_dir;
	struct stat stat_buf;
	int res, size;

	runtime_dir = getenv("PULSE_RUNTIME_PATH");
	if (runtime_dir == NULL)
		runtime_dir = getenv("XDG_RUNTIME_DIR");

	if (runtime_dir == NULL) {
		pw_log_error(NAME": could not find a suitable runtime directory in"
				"$PULSE_RUNTIME_PATH and $XDG_RUNTIME_DIR");
		return -ENOENT;
	}

	size = snprintf(buf, buflen, "%s/%s", runtime_dir, dir);
	if (size < 0)
		return -errno;
	if ((size_t) size >= buflen) {
		pw_log_error(NAME": path %s/%s too long", runtime_dir, dir);
		return -ENAMETOOLONG;
	}

	if (stat(buf, &stat_buf) < 0) {
		res = -errno;
		if (res != -ENOENT) {
			pw_log_error(NAME": stat() %s failed: %m", buf);
			return res;
		}
		if (mkdir(buf, 0700) < 0) {
			res = -errno;
			pw_log_error(NAME": mkdir() %s failed: %m", buf);
			return res;
		}
		pw_log_info(NAME": created %s", buf);
	} else if (!S_ISDIR(stat_buf.st_mode)) {
		pw_log_error(NAME": %s is not a directory", buf);
		return -ENOTDIR;
	}
	return 0;
}

int check_flatpak(struct client *client, pid_t pid)
{
	char root_path[2048];
	int root_fd, info_fd, res;
	struct stat stat_buf;

	sprintf(root_path, "/proc/%ld/root", (long) pid);
	root_fd = openat(AT_FDCWD, root_path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
	if (root_fd == -1) {
		res = -errno;
		if (res == -EACCES) {
			struct statfs buf;
			/* Access to the root dir isn't allowed. This can happen if the root is on a fuse
			 * filesystem, such as in a toolbox container. We will never have a fuse rootfs
			 * in the flatpak case, so in that case its safe to ignore this and
			 * continue to detect other types of apps. */
			if (statfs(root_path, &buf) == 0 &&
			    buf.f_type == 0x65735546) /* FUSE_SUPER_MAGIC */
				return 0;
		}
		/* Not able to open the root dir shouldn't happen. Probably the app died and
		 * we're failing due to /proc/$pid not existing. In that case fail instead
		 * of treating this as privileged. */
		pw_log_info("failed to open \"%s\": %s", root_path, spa_strerror(res));
		return res;
	}
	info_fd = openat(root_fd, ".flatpak-info", O_RDONLY | O_CLOEXEC | O_NOCTTY);
	close(root_fd);
	if (info_fd == -1) {
		if (errno == ENOENT) {
			pw_log_debug("no .flatpak-info, client on the host");
			/* No file => on the host */
			return 0;
		}
		res = -errno;
		pw_log_error("error opening .flatpak-info: %m");
		return res;
	}
	if (fstat(info_fd, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
		/* Some weird fd => failure, assume sandboxed */
		pw_log_error("error fstat .flatpak-info: %m");
	}
	close(info_fd);
	return 1;
}

pid_t get_client_pid(struct client *client, int client_fd)
{
	socklen_t len;
#if defined(__linux__)
	struct ucred ucred;
	len = sizeof(ucred);
	if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &len) < 0) {
		pw_log_warn("client %p: no peercred: %m", client);
	} else
		return ucred.pid;
#elif defined(__FreeBSD__)
	struct xucred xucred;
	len = sizeof(xucred);
	if (getsockopt(client_fd, 0, LOCAL_PEERCRED, &xucred, &len) < 0) {
		pw_log_warn("client %p: no peercred: %m", client);
	} else {
#if __FreeBSD__ >= 13
		return xucred.cr_pid;
#endif
	}
#endif
	return 0;
}

const char *get_server_name(struct pw_context *context)
{
	const char *name = NULL;
	const struct pw_properties *props = pw_context_get_properties(context);

	if (props)
		name = pw_properties_get(props, PW_KEY_REMOTE_NAME);
	if (name == NULL || name[0] == '\0')
		name = getenv("PIPEWIRE_REMOTE");
	if (name == NULL || name[0] == '\0')
		name = PW_DEFAULT_REMOTE;
	return name;
}

int create_pid_file(void) {
	char pid_file[PATH_MAX];
	FILE *f;
	int res;

	if ((res = get_runtime_dir(pid_file, sizeof(pid_file), "pulse")) < 0)
		return res;

	if (strlen(pid_file) > PATH_MAX - sizeof("/pid")) {
		pw_log_error(NAME": path too long: %s/pid", pid_file);
		return -ENAMETOOLONG;
	}

	strcat(pid_file, "/pid");

	if ((f = fopen(pid_file, "w")) == NULL) {
		res = -errno;
		pw_log_error(NAME": failed to open pid file: %m");
		return res;
	}

	fprintf(f, "%lu\n", (unsigned long) getpid());
	fclose(f);

	return 0;
}
