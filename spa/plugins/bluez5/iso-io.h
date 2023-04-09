/* Spa Bluez5 ISO I/O */
/* SPDX-FileCopyrightText: Copyright © 2023 Pauli Virtanen */
/* SPDX-License-Identifier: MIT */

#ifndef SPA_BLUEZ5_ISO_IO_H
#define SPA_BLUEZ5_ISO_IO_H

#include <spa/utils/defs.h>
#include <spa/support/loop.h>
#include <spa/support/log.h>
#include <spa/node/io.h>

/**
 * ISO I/O.
 *
 * Synchronizes related writes from different streams in the same group
 * to occur at same real time instant (or not at all).
 */
struct spa_bt_iso_io
{
	uint64_t now;		/**< Reference time position of next packet (read-only) */
	uint64_t duration;	/**< ISO interval duration in ns (read-only) */
	bool resync;		/**< Resync position for next packet; (pull callback sets to
				 * false when done) */

	uint32_t timestamp;	/**< Packet timestamp (set by pull callback) */
	uint8_t buf[4096];	/**< Packet data (set by pull callback) */
	size_t size;		/**< Packet size (set by pull callback) */

	void *user_data;
};

typedef void (*spa_bt_iso_io_pull_t)(struct spa_bt_iso_io *io);

struct spa_bt_iso_io *spa_bt_iso_io_create(int fd, bool sink, uint8_t cig, uint32_t interval,
		struct spa_log *log, struct spa_loop *data_loop, struct spa_system *data_system);
struct spa_bt_iso_io *spa_bt_iso_io_attach(struct spa_bt_iso_io *io, int fd, bool sink);
void spa_bt_iso_io_destroy(struct spa_bt_iso_io *io);
void spa_bt_iso_io_set_cb(struct spa_bt_iso_io *io, spa_bt_iso_io_pull_t pull, void *user_data);

#endif
