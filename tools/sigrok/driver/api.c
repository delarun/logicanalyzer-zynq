/*
 * This file is part of the libsigrok project.
 *
 * Antminer S9 logic analyzer (network, protocol S9LA v1 — see sw/la/PROTOCOL.md).
 *
 * Connection: conn=tcp-raw/<host>/<port>, tcp/<host>/<port>, <host>:<port> or <host>
 * (default port 5555). Example:
 *   sigrok-cli -d antminer-s9-la:conn=192.168.10.224 --samples 1M --config samplerate=10M
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

/* 100 MHz / N */
static const uint64_t samplerates[] = {
	SR_KHZ(1), SR_KHZ(2), SR_KHZ(5), SR_KHZ(10), SR_KHZ(20), SR_KHZ(50),
	SR_KHZ(100), SR_KHZ(200), SR_KHZ(500),
	SR_MHZ(1), SR_MHZ(2), SR_MHZ(4), SR_MHZ(5), SR_MHZ(10), SR_MHZ(20),
	SR_MHZ(25), SR_MHZ(50), SR_MHZ(100),
};

static const char *patterns[] = {
	"Inputs",
	"Counter (self-test)",
	"Inputs + TX test signals",
};

static const char *channel_names[S9LA_MAX_CHANNELS] = {
	"J1_RX", "J2_RX", "J3_RX", "J4_RX", "J5_RX", "J6_RX", "J7_RX", "J8_RX", "J9_RX",
	"J1_PLUG", "J2_PLUG", "J3_PLUG", "J4_PLUG", "J5_PLUG", "J6_PLUG", "J7_PLUG",
	"J8_PLUG", "J9_PLUG",
	"FAN1", "FAN2", "FAN3", "FAN4", "FAN5", "FAN6",
	"SCL1", "SDA1", "SCL2", "SDA2",
	"CH28", "CH29", "CH30", "CH31",
};

static struct sr_dev_driver antminer_s9_la_driver_info;

/* conn → host, port. Accepts tcp-raw/h/p, tcp/h/p, h/p, h:p, h. */
static gboolean parse_conn(const char *conn, char **host, char **port)
{
	const char *s = conn;
	gchar **parts;

	if (g_str_has_prefix(s, "tcp-raw/"))
		s += 8;
	else if (g_str_has_prefix(s, "tcp/"))
		s += 4;

	parts = g_strsplit_set(s, "/:", 3);
	if (!parts[0] || !*parts[0]) {
		g_strfreev(parts);
		return FALSE;
	}
	*host = g_strdup(parts[0]);
	*port = g_strdup(parts[1] && *parts[1] ? parts[1] : S9LA_DEFAULT_PORT);
	g_strfreev(parts);
	return TRUE;
}

static uint64_t kv_u64(const char *text, const char *key, uint64_t def)
{
	gchar *pat = g_strdup_printf(" %s=", key);
	const char *p = strstr(text, pat);
	uint64_t v = def;

	if (p)
		v = g_ascii_strtoull(p + strlen(pat), NULL, 10);
	g_free(pat);
	return v;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const char *conn = NULL;
	char *host, *port;
	char reply[256];
	GSList *l;

	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}
	if (!conn)
		return NULL;   /* no network discovery: conn is required */

	if (!parse_conn(conn, &host, &port)) {
		sr_err("Bad conn '%s'.", conn);
		return NULL;
	}

	devc = g_malloc0(sizeof(*devc));
	devc->tcp = sr_tcp_dev_inst_new(host, port);
	g_free(host);
	g_free(port);

	if (sr_tcp_connect(devc->tcp) != SR_OK) {
		sr_err("Cannot connect to %s.", conn);
		goto fail;
	}
	if (s9la_command(devc, "ID", reply, sizeof(reply)) != SR_OK ||
	    !g_str_has_prefix(reply, "S9LA ")) {
		sr_err("No S9LA device at %s.", conn);
		sr_tcp_disconnect(devc->tcp);
		goto fail;
	}
	sr_tcp_disconnect(devc->tcp);

	devc->num_channels = (uint32_t)kv_u64(reply, "channels", 28);
	if (devc->num_channels == 0 || devc->num_channels > S9LA_MAX_CHANNELS)
		devc->num_channels = S9LA_MAX_CHANNELS;
	devc->clock_hz = kv_u64(reply, "clock", SR_MHZ(100));
	devc->memory = kv_u64(reply, "memory", 256 * 1024 * 1024);
	devc->samplerate = SR_MHZ(10);
	devc->limit_samples = SR_MHZ(1);
	devc->capture_ratio = 10;
	devc->pattern = S9LA_PATTERN_INPUTS;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Bitmain");
	sdi->model = g_strdup("Antminer S9 LA");
	sdi->version = g_strdup("1");
	sdi->connection_id = g_strdup(conn);
	sdi->driver = &antminer_s9_la_driver_info;
	sdi->priv = devc;

	for (uint32_t i = 0; i < devc->num_channels; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_names[i]);

	sr_info("Found S9LA at %s: %u channels, %" PRIu64 " bytes of memory.",
		conn, devc->num_channels, devc->memory);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

fail:
	sr_tcp_dev_inst_free(devc->tcp);
	g_free(devc);
	return NULL;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	return sr_tcp_connect(devc->tcp);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	return sr_tcp_disconnect(devc->tcp);
}

static void clear_helper(struct dev_context *devc)
{
	sr_tcp_dev_inst_free(devc->tcp);
	g_free(devc->rx);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(patterns[devc->pattern]);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t v;
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		v = g_variant_get_uint64(data);
		if (v == 0 || v > devc->clock_hz)
			return SR_ERR_ARG;
		devc->samplerate = v;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		v = g_variant_get_uint64(data);
		if (v == 0 || v > devc->memory)
			return SR_ERR_ARG;
		devc->limit_samples = v;
		break;
	case SR_CONF_CAPTURE_RATIO:
		v = g_variant_get_uint64(data);
		if (v > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = v;
		break;
	case SR_CONF_PATTERN_MODE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(patterns))) < 0)
			return SR_ERR_ARG;
		devc->pattern = idx;
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		*data = std_gvar_tuple_u64(1, devc->memory);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(patterns));
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int convert_trigger(const struct sr_dev_inst *sdi, uint32_t *tmask,
	uint32_t *tval, uint32_t *trise, uint32_t *tfall)
{
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l;

	*tmask = *tval = *trise = *tfall = 0;
	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;
	if (g_slist_length(trigger->stages) > 1) {
		sr_err("Only one trigger stage is supported.");
		return SR_ERR_NA;
	}
	if (!trigger->stages)
		return SR_OK;
	stage = trigger->stages->data;
	for (l = stage->matches; l; l = l->next) {
		match = l->data;
		if (!match->channel->enabled)
			continue;
		uint32_t bit = 1u << match->channel->index;
		switch (match->match) {
		case SR_TRIGGER_ONE:
			*tmask |= bit;
			*tval |= bit;
			break;
		case SR_TRIGGER_ZERO:
			*tmask |= bit;
			break;
		case SR_TRIGGER_RISING:
			*trise |= bit;
			break;
		case SR_TRIGGER_FALLING:
			*tfall |= bit;
			break;
		default:
			sr_err("Unsupported trigger type %d.", match->match);
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_channel *ch;
	uint32_t tmask, tval, trise, tfall;
	int maxidx = -1;
	uint64_t limit, pre;
	char reply[256], *cmd;
	GSList *l;
	int ret;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_LOGIC && ch->enabled && ch->index > maxidx)
			maxidx = ch->index;
	}
	if (maxidx < 0) {
		sr_err("No channels enabled.");
		return SR_ERR;
	}
	devc->unit = maxidx < 8 ? 1 : maxidx < 16 ? 2 : 4;

	if ((ret = convert_trigger(sdi, &tmask, &tval, &trise, &tfall)) != SR_OK)
		return ret;

	limit = devc->limit_samples;
	if (limit * devc->unit > devc->memory) {
		limit = devc->memory / devc->unit;
		sr_warn("Limiting capture to %" PRIu64 " samples (device memory).", limit);
	}
	pre = 0;
	if (tmask || trise || tfall) {
		pre = limit * devc->capture_ratio / 100;
		if (pre >= limit)
			pre = limit - 1;
	}

	cmd = g_strdup_printf("CONF rate=%" PRIu64 " unit=%u samples=%" PRIu64
		" pre=%" PRIu64 " tmask=%x tval=%x trise=%x tfall=%x pattern=%d",
		devc->samplerate, devc->unit, limit, pre, tmask, tval, trise, tfall,
		devc->pattern);
	ret = s9la_command(devc, cmd, reply, sizeof(reply));
	g_free(cmd);
	if (ret != SR_OK)
		return ret;
	if (!g_str_has_prefix(reply, "OK")) {
		sr_err("Device rejected configuration: %s", reply);
		return SR_ERR;
	}

	if ((ret = s9la_command(devc, "START", reply, sizeof(reply))) != SR_OK)
		return ret;
	if (!g_str_has_prefix(reply, "OK")) {
		sr_err("Device rejected START: %s", reply);
		return SR_ERR;
	}

	g_free(devc->rx);
	devc->rx = g_malloc(S9LA_RX_SIZE);
	devc->rx_len = 0;
	devc->total = 0;
	devc->sent = 0;
	devc->trigger_at = -1;
	devc->trigger_sent = FALSE;
	devc->stopping = FALSE;
	devc->finished = FALSE;

	std_session_send_df_header(sdi);

	return sr_tcp_source_add(sdi->session, devc->tcp, G_IO_IN, 100,
		s9la_receive_data, (void *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->finished || devc->stopping)
		return SR_OK;
	/* The device answers with H/D/E frames; s9la_receive_data() ends the session. */
	devc->stopping = TRUE;
	devc->stop_deadline = g_get_monotonic_time() + S9LA_STOP_TIMEOUT_US;
	return s9la_send(devc, "STOP");
}

static struct sr_dev_driver antminer_s9_la_driver_info = {
	.name = "antminer-s9-la",
	.longname = "Antminer S9 logic analyzer",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(antminer_s9_la_driver_info);
