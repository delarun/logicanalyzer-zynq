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

#include <config.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "protocol.h"

static int sock(struct dev_context *devc)
{
	return devc->tcp ? devc->tcp->sock_fd : -1;
}

/* Receive exactly len bytes, waiting at most timeout_ms in total. */
static int recv_exact(struct dev_context *devc, uint8_t *buf, size_t len,
	int timeout_ms)
{
	gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
	size_t got = 0;

	while (got < len) {
		struct pollfd pfd = { .fd = sock(devc), .events = POLLIN };
		gint64 left = (deadline - g_get_monotonic_time()) / 1000;
		if (left <= 0)
			return SR_ERR_TIMEOUT;
		int rc = poll(&pfd, 1, (int)left);
		if (rc < 0 && errno == EINTR)
			continue;
		if (rc <= 0)
			return SR_ERR_TIMEOUT;
		ssize_t n = recv(sock(devc), buf + got, len - got, 0);
		if (n <= 0)
			return SR_ERR_IO;
		got += (size_t)n;
	}
	return SR_OK;
}

SR_PRIV int s9la_send(struct dev_context *devc, const char *cmd)
{
	char *line = g_strdup_printf("%s\n", cmd);
	size_t len = strlen(line), done = 0;
	int ret = SR_OK;

	sr_dbg("-> %s", cmd);
	while (done < len) {
		/* MSG_NOSIGNAL: closed connection must not kill the client with SIGPIPE. */
		ssize_t n = send(sock(devc), line + done, len - done, MSG_NOSIGNAL);
		if (n <= 0) {
			ret = SR_ERR_IO;
			break;
		}
		done += (size_t)n;
	}
	g_free(line);
	return ret;
}

/*
 * Send a command and wait for its 'R' reply. Other frames received in the
 * meantime are discarded (there should be none outside of an acquisition).
 */
static int command_once(struct dev_context *devc, const char *cmd,
	char *reply, size_t reply_len);

/*
 * The device serves one client and drops the old connection when a new one
 * arrives (e.g. sigrok-cli while PulseView is open). On I/O error reconnect
 * once and retry.
 */
SR_PRIV int s9la_command(struct dev_context *devc, const char *cmd,
	char *reply, size_t reply_len)
{
	int ret = command_once(devc, cmd, reply, reply_len);

	if (ret == SR_ERR_IO) {
		sr_info("Connection lost, reconnecting.");
		sr_tcp_disconnect(devc->tcp);
		if (sr_tcp_connect(devc->tcp) != SR_OK)
			return SR_ERR_IO;
		ret = command_once(devc, cmd, reply, reply_len);
	}
	return ret;
}

static int command_once(struct dev_context *devc, const char *cmd,
	char *reply, size_t reply_len)
{
	uint8_t hdr[S9LA_HDR_LEN];
	int ret;

	if ((ret = s9la_send(devc, cmd)) != SR_OK)
		return ret;

	for (;;) {
		if ((ret = recv_exact(devc, hdr, sizeof(hdr), S9LA_CMD_TIMEOUT_MS)) != SR_OK) {
			sr_err("No reply to '%s'.", cmd);
			return ret;
		}
		if (hdr[0] != 'S' || hdr[1] != '9') {
			sr_err("Bad frame header in reply to '%s'.", cmd);
			return SR_ERR_DATA;
		}
		uint32_t len = RL32(hdr + 4);
		uint8_t *data = g_malloc(len + 1);
		if ((ret = recv_exact(devc, data, len, S9LA_CMD_TIMEOUT_MS)) != SR_OK) {
			g_free(data);
			return ret;
		}
		data[len] = 0;
		if (hdr[2] == 'R') {
			sr_dbg("<- %s", (char *)data);
			if (reply && reply_len) {
				g_strlcpy(reply, (char *)data, reply_len);
			}
			g_free(data);
			return SR_OK;
		}
		sr_dbg("Dropping frame '%c' (%u bytes) while waiting for reply.",
			hdr[2], len);
		g_free(data);
	}
}

static void finish(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->finished)
		return;
	devc->finished = TRUE;
	std_session_send_df_end(sdi);
	sr_session_source_remove(sdi->session, sock(devc));
	g_free(devc->rx);
	devc->rx = NULL;
	devc->rx_len = 0;
}

static void send_logic(const struct sr_dev_inst *sdi, const uint8_t *data,
	uint64_t nsamples)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!nsamples)
		return;
	logic.length = nsamples * devc->unit;
	logic.unitsize = devc->unit;
	logic.data = (void *)data;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	sr_session_send(sdi, &packet);
}

static void handle_data(const struct sr_dev_inst *sdi, const uint8_t *data,
	uint32_t len)
{
	struct dev_context *devc = sdi->priv;
	uint64_t n = len / devc->unit;

	if (devc->trigger_at >= 0 && !devc->trigger_sent &&
	    devc->sent + n > (uint64_t)devc->trigger_at) {
		uint64_t before = (uint64_t)devc->trigger_at - devc->sent;
		send_logic(sdi, data, before);
		std_session_send_df_trigger(sdi);
		devc->trigger_sent = TRUE;
		send_logic(sdi, data + before * devc->unit, n - before);
	} else {
		send_logic(sdi, data, n);
	}
	devc->sent += n;
}

static void handle_header(struct dev_context *devc, const char *text)
{
	gchar **kv = g_strsplit(text, " ", 0);

	for (gchar **p = kv; *p; p++) {
		if (g_str_has_prefix(*p, "samples="))
			devc->total = g_ascii_strtoull(*p + 8, NULL, 10);
		else if (g_str_has_prefix(*p, "trigger="))
			devc->trigger_at = g_ascii_strtoll(*p + 8, NULL, 10);
		else if (g_str_has_prefix(*p, "unit="))
			devc->unit = (unsigned int)g_ascii_strtoull(*p + 5, NULL, 10);
	}
	g_strfreev(kv);
	if (devc->unit != 1 && devc->unit != 2 && devc->unit != 4)
		devc->unit = 1;
	sr_dbg("Capture: %" PRIu64 " samples, trigger at %" PRId64 ", %u bytes/sample.",
		devc->total, devc->trigger_at, devc->unit);
}

SR_PRIV int s9la_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc;
	size_t pos;

	(void)fd;

	if (!sdi || !(devc = sdi->priv) || devc->finished)
		return TRUE;

	if (revents & G_IO_IN) {
		for (;;) {
			if (devc->rx_len == S9LA_RX_SIZE)
				break;
			ssize_t n = recv(sock(devc), devc->rx + devc->rx_len,
				S9LA_RX_SIZE - devc->rx_len, MSG_DONTWAIT);
			if (n == 0) {
				sr_err("Device closed the connection.");
				finish(sdi);
				return TRUE;
			}
			if (n < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
					break;
				sr_err("Receive error: %s.", g_strerror(errno));
				finish(sdi);
				return TRUE;
			}
			devc->rx_len += (size_t)n;
		}

		pos = 0;
		while (devc->rx_len - pos >= S9LA_HDR_LEN) {
			const uint8_t *hdr = devc->rx + pos;
			if (hdr[0] != 'S' || hdr[1] != '9') {
				sr_err("Lost frame sync.");
				finish(sdi);
				return TRUE;
			}
			uint32_t len = RL32(hdr + 4);
			if (len > S9LA_RX_SIZE - S9LA_HDR_LEN) {
				sr_err("Frame too large: %u bytes.", len);
				finish(sdi);
				return TRUE;
			}
			if (devc->rx_len - pos < S9LA_HDR_LEN + len)
				break;
			const uint8_t *data = hdr + S9LA_HDR_LEN;
			char *text;

			switch (hdr[2]) {
			case 'H':
				text = g_strndup((const char *)data, len);
				handle_header(devc, text);
				g_free(text);
				break;
			case 'D':
				handle_data(sdi, data, len);
				break;
			case 'E':
				text = g_strndup((const char *)data, len);
				if (g_str_has_prefix(text, "ERR"))
					sr_err("Device: %s", text);
				else
					sr_dbg("Capture end: %s (%" PRIu64 " samples).",
						text, devc->sent);
				g_free(text);
				finish(sdi);
				return TRUE;
			default:
				sr_dbg("Ignoring frame '%c'.", hdr[2]);
				break;
			}
			pos += S9LA_HDR_LEN + len;
		}
		if (pos) {
			memmove(devc->rx, devc->rx + pos, devc->rx_len - pos);
			devc->rx_len -= pos;
		}
	}

	if (devc->stopping && g_get_monotonic_time() > devc->stop_deadline) {
		sr_warn("Device did not finish the capture after STOP.");
		finish(sdi);
	}

	return TRUE;
}
