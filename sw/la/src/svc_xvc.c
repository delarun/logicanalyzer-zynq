// XVC 1.0 (Xilinx Virtual Cable), TCP 2542: JTAG для Vivado/XSCT через линии J9.
#include <stdio.h>
#include <string.h>

#include "probe.h"
#include "services.h"
#include "tcpsrv.h"

#define XVC_MAX 2048    // байт на вектор

static tcpsrv_t srv;
static u32 xvc_hz = 10000000;
static u8 tdo[XVC_MAX];

static void process(tcpsrv_t *s)
{
    for (;;) {
        const u8 *rx = s->rx;
        u32 len = s->rxlen;
        if (len < 2) break;

        if (!memcmp(rx, "ge", 2)) {
            if (len < 8) break;
            if (srv_space(s) < 32) break;
            char r[32];
            int n = snprintf(r, sizeof r, "xvcServer_v1.0:%d\n", XVC_MAX);
            srv_send(s, r, n);
            srv_consume(s, 8);
        } else if (!memcmp(rx, "se", 2)) {
            if (len < 11) break;
            if (srv_space(s) < 4) break;
            u32 period = le32(rx + 7);
            if (period == 0) period = 1;
            xvc_hz = probe_hz_actual(1000000000u / period);
            u8 r[4];
            put_le32(r, 1000000000u / xvc_hz);
            srv_send(s, r, 4);
            srv_consume(s, 11);
        } else if (!memcmp(rx, "sh", 2)) {
            if (len < 10) break;
            u32 nbits = le32(rx + 6), nbytes = (nbits + 7) / 8;
            if (nbytes > XVC_MAX) { srv_close(s); return; }
            if (len < 10 + 2 * nbytes) break;
            if (srv_space(s) < nbytes) break;
            probe_mode(PM_JTAG, xvc_hz);
            probe_shift(PM_JTAG, nbits, rx + 10, rx + 10 + nbytes, NULL, tdo);
            srv_send(s, tdo, nbytes);
            srv_consume(s, 10 + 2 * nbytes);
        } else {
            srv_close(s);
            return;
        }
    }
    srv_flush(s);
}

void svc_xvc_init(void)
{
    srv.name = "xvc";
    srv.port = 2542;
    srv.process = process;
    srv_start(&srv);
}

void svc_xvc_poll(void) { srv_poll(&srv); }
