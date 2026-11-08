/*
 * This file is part of the libsigrok project.
 *
 * Antminer S9 logic analyzer (network, protocol S9LA v1 — see sw/la/PROTOCOL.md).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef LIBSIGROK_HARDWARE_ANTMINER_S9_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ANTMINER_S9_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "antminer-s9-la"

#define S9LA_DEFAULT_PORT	"5555"
#define S9LA_MAX_CHANNELS	32
#define S9LA_HDR_LEN		8
#define S9LA_RX_SIZE		(1024 * 1024)
#define S9LA_CMD_TIMEOUT_MS	3000
#define S9LA_STOP_TIMEOUT_US	(5 * 1000 * 1000)

enum s9la_pattern {
	S9LA_PATTERN_INPUTS = 0,
	S9LA_PATTERN_COUNTER = 1,
	S9LA_PATTERN_TXGEN = 2,
};

struct dev_context {
	struct sr_tcp_dev_inst *tcp;

	/* From the ID reply. */
	uint32_t num_channels;
	uint64_t clock_hz;
	uint64_t memory;

	/* Settings. */
	uint64_t samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;
	int pattern;

	/* Acquisition state. */
	unsigned int unit;
	uint8_t *rx;
	size_t rx_len;
	uint64_t total;
	uint64_t sent;
	int64_t trigger_at;
	gboolean trigger_sent;
	gboolean stopping;
	gboolean finished;
	gint64 stop_deadline;
};

SR_PRIV int s9la_send(struct dev_context *devc, const char *cmd);
SR_PRIV int s9la_command(struct dev_context *devc, const char *cmd,
	char *reply, size_t reply_len);
SR_PRIV int s9la_receive_data(int fd, int revents, void *cb_data);

#endif
