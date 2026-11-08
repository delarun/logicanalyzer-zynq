// CMSIS-DAP v2 поверх TCP (как в OpenOCD: cmsis-dap backend tcp), порт 4441.
// Пакет: "DAP\0" (u32 LE 0x00504144), u16 длина, u8 тип (1 запрос, 2 ответ), u8 0, данные.
// JTAG — DAP_JTAG_Sequence; SWD — DAP_Transfer/TransferBlock (по образцу DAP.c из CMSIS).
#include <string.h>

#include "probe.h"
#include "services.h"
#include "tcpsrv.h"
#include "xil_printf.h"

#define DAP_SIG        0x00504144u
#define DAP_PKT        1024
#define HDR            8

// команды
#define ID_DAP_Info             0x00
#define ID_DAP_HostStatus       0x01
#define ID_DAP_Connect          0x02
#define ID_DAP_Disconnect       0x03
#define ID_DAP_TransferConfigure 0x04
#define ID_DAP_Transfer         0x05
#define ID_DAP_TransferBlock    0x06
#define ID_DAP_TransferAbort    0x07
#define ID_DAP_WriteABORT       0x08
#define ID_DAP_Delay            0x09
#define ID_DAP_ResetTarget      0x0A
#define ID_DAP_SWJ_Pins         0x10
#define ID_DAP_SWJ_Clock        0x11
#define ID_DAP_SWJ_Sequence     0x12
#define ID_DAP_SWD_Configure    0x13
#define ID_DAP_JTAG_Sequence    0x14
#define ID_DAP_JTAG_Configure   0x15
#define ID_DAP_JTAG_IDCODE      0x16
#define ID_DAP_SWD_Sequence     0x1D

#define DAP_OK     0x00
#define DAP_ERROR  0xFF

// запрос Transfer
#define T_APnDP        (1u << 0)
#define T_RnW          (1u << 1)
#define T_MATCH_VALUE  (1u << 4)
#define T_MATCH_MASK   (1u << 5)
// ответ Transfer
#define ACK_OK         1u
#define ACK_WAIT       2u
#define ACK_FAULT      4u
#define ACK_ERROR      8u
#define ACK_MISMATCH   16u

#define DP_RDBUFF      0x0Cu    // A[3:2] = 3

static tcpsrv_t srv;
static u8 out[HDR + DAP_PKT];

static struct {
    int  port;              // 0 — не подключено, 1 SWD, 2 JTAG
    u32  hz;
    u32  idle_cycles;
    u32  wait_retry;
    u32  match_retry;
    u32  match_mask;
    u32  turnaround;
    int  data_phase;
} dap = { 0, 1000000, 0, 100, 0, 0, 1, 0 };

static int cur_mode(void) { return dap.port == 2 ? PM_JTAG : PM_SWD; }

// ---------------------------------------------------------------------
// SWD
// ---------------------------------------------------------------------
static u32 swd_bits(u32 n, u32 val, int drive, int read)
{
    return probe_shift32(PM_SWD, n, val, 0, drive ? 0xFFFFFFFFu : 0u, read);
}

static u32 parity32(u32 v) { return (u32)__builtin_popcount(v) & 1u; }

static u32 swd_transfer(u32 req, u32 *data)
{
    u32 r = req & 0x0F;
    u32 hdr = 0x81u | (r << 1) | (parity32(r) << 5);
    u32 t = dap.turnaround;

    swd_bits(8, hdr, 1, 0);
    u32 in = swd_bits(t + 3, 0, 0, 1);
    u32 ack = (in >> t) & 7u;

    if (ack == ACK_OK) {
        if (req & T_RnW) {
            u32 d = swd_bits(32, 0, 0, 1);
            u32 p = swd_bits(1 + t, 0, 0, 1) & 1u;
            if (p != parity32(d)) ack = ACK_ERROR;
            if (data) *data = d;
        } else {
            swd_bits(t, 0, 0, 0);
            u32 d = data ? *data : 0;
            swd_bits(32, d, 1, 0);
            swd_bits(1, parity32(d), 1, 0);
        }
        u32 idle = dap.idle_cycles;
        while (idle) {
            u32 n = idle > 32 ? 32 : idle;
            swd_bits(n, 0, 1, 0);       // такты простоя — SWDIO в 0 (1 был бы стартом пакета)
            idle -= n;
        }
        return ack;
    }

    if (ack == ACK_WAIT || ack == ACK_FAULT) {
        if (dap.data_phase && (req & T_RnW)) swd_bits(32, 0, 0, 0), swd_bits(1, 0, 0, 0);
        if (t) swd_bits(t, 0, 0, 0);
        if (dap.data_phase && !(req & T_RnW)) swd_bits(32, 0, 1, 0), swd_bits(1, 0, 1, 0);
        return ack;
    }

    // ошибка протокола: пропустить фазу данных
    swd_bits(32, 0, 0, 0);
    swd_bits(1 + t, 0, 0, 0);
    return ack;
}

static u32 swd_retry(u32 req, u32 *data)
{
    u32 n = dap.wait_retry, ack;
    do {
        ack = swd_transfer(req, data);
    } while (ack == ACK_WAIT && n--);
    return ack;
}

// ---------------------------------------------------------------------
// Команды
// ---------------------------------------------------------------------
static u32 info(const u8 *rq, u8 *rs)
{
    const char *str = NULL;
    rs[0] = ID_DAP_Info;
    switch (rq[1]) {
    case 0x01: str = "Antminer S9"; break;
    case 0x02: str = "S9 probe CMSIS-DAP"; break;
    case 0x03: str = "S9PROBE0001"; break;
    case 0x04: str = "2.1.0"; break;
    case 0x09: str = "1.0"; break;
    case 0xF0: rs[1] = 1; rs[2] = 0x03; return 3;          // SWD + JTAG
    case 0xFE: rs[1] = 1; rs[2] = 4; return 3;             // пакетов в очереди
    case 0xFF: rs[1] = 2; put_le16(rs + 2, DAP_PKT); return 4;
    default: rs[1] = 0; return 2;
    }
    u32 n = strlen(str) + 1;
    rs[1] = n;
    memcpy(rs + 2, str, n);
    return 2 + n;
}

static void pins_release(void)
{
    probe_mode(PM_NONE, 0);
    probe_gpio((1u << PIN_TCK) | (1u << PIN_TMS) | (1u << PIN_TDI), 0, 0);
    probe_od(PIN_NRST, 1);
}

static u32 swj_pins(const u8 *rq, u8 *rs)
{
    u32 val = rq[1], sel = rq[2], wait = le32(rq + 3);
    u32 m = 0, v = 0;
    if (sel & 0x01) { m |= 1u << PIN_TCK; v |= (val & 0x01) ? 1u << PIN_TCK : 0; }
    if (sel & 0x02) { m |= 1u << PIN_TMS; v |= (val & 0x02) ? 1u << PIN_TMS : 0; }
    if (sel & 0x04) { m |= 1u << PIN_TDI; v |= (val & 0x04) ? 1u << PIN_TDI : 0; }
    if (m) {
        probe_mode(PM_NONE, 0);         // эти линии — временно под GPIO
        probe_gpio(m, v, m);
    }
    if (sel & 0x80) probe_od(PIN_NRST, (val & 0x80) != 0);
    if (wait) {
        if (wait > 3000000) wait = 3000000;
        u32 want = val & sel & 0x87, got = 0;
        for (u32 us = 0; us < wait; us += 10) {
            u32 in = probe_gpio_in();
            got = ((in >> PIN_TCK) & 1) | ((in >> PIN_TMS) & 1) << 1 | ((in >> PIN_TDI) & 1) << 2
                | ((in >> PIN_NRST) & 1) << 7;
            if ((got & sel & 0x87) == want) break;
            probe_delay_us(10);
        }
    }
    u32 in = probe_gpio_in();
    rs[0] = ID_DAP_SWJ_Pins;
    rs[1] = ((in >> PIN_TCK) & 1) | ((in >> PIN_TMS) & 1) << 1 | ((in >> PIN_TDI) & 1) << 2
          | ((in >> PIN_TDO) & 1) << 3 | (1u << 5) | ((in >> PIN_NRST) & 1) << 7;
    return 2;
}

// длина запроса Transfer до места, где разбор остановился, не нужна — ответ формируется сразу
static u32 transfer(const u8 *rq, u32 rqlen, u8 *rs)
{
    u32 count = rq[2];
    const u8 *p = rq + 3, *end = rq + rqlen;
    u8 *o = rs + 3;
    u32 done = 0, ack = 0, data = 0;
    int post_read = 0, check_write = 0;

    rs[0] = ID_DAP_Transfer;
    if (dap.port != 1) { rs[1] = 0; rs[2] = 0; return 3; }     // только SWD
    probe_mode(PM_SWD, dap.hz);

    while (count-- && p < end) {
        u32 req = *p++;
        if (req & T_RnW) {
            if (post_read) {
                if ((req & (T_APnDP | T_MATCH_VALUE)) == T_APnDP) {
                    ack = swd_retry(req, &data);         // данные прошлого AP-чтения, новое — в пути
                } else {
                    post_read = 0;
                    ack = swd_retry(DP_RDBUFF | T_RnW, &data);
                }
                if (ack != ACK_OK) break;
                put_le32(o, data); o += 4;
            }
            if (req & T_MATCH_VALUE) {
                u32 match = le32(p); p += 4;
                u32 retry = dap.match_retry;
                if (req & T_APnDP) {
                    ack = swd_retry(req, NULL);
                    if (ack != ACK_OK) break;
                }
                do {
                    ack = swd_retry(req, &data);
                    if (ack != ACK_OK) break;
                } while (((data & dap.match_mask) != match) && retry--);
                if (ack == ACK_OK && (data & dap.match_mask) != match) ack |= ACK_MISMATCH;
                if (ack != ACK_OK) break;
            } else if (!post_read) {
                if (req & T_APnDP) {
                    ack = swd_retry(req, NULL);
                    if (ack != ACK_OK) break;
                    post_read = 1;
                } else {
                    ack = swd_retry(req, &data);
                    if (ack != ACK_OK) break;
                    put_le32(o, data); o += 4;
                }
            }
            check_write = 0;
        } else {
            if (post_read) {
                ack = swd_retry(DP_RDBUFF | T_RnW, &data);
                if (ack != ACK_OK) break;
                put_le32(o, data); o += 4;
                post_read = 0;
            }
            data = le32(p); p += 4;
            if (req & T_MATCH_MASK) {
                dap.match_mask = data;
                ack = ACK_OK;
            } else {
                ack = swd_retry(req, &data);
                if (ack != ACK_OK) break;
                check_write = 1;
            }
        }
        done++;
    }

    if (ack == ACK_OK) {
        if (post_read) {
            ack = swd_retry(DP_RDBUFF | T_RnW, &data);
            if (ack == ACK_OK) { put_le32(o, data); o += 4; }
        } else if (check_write) {
            ack = swd_retry(DP_RDBUFF | T_RnW, NULL);
        }
    }
    rs[1] = done;
    rs[2] = ack;
    return o - rs;
}

static u32 transfer_block(const u8 *rq, u8 *rs)
{
    u32 count = le16(rq + 2), req = rq[4];
    const u8 *p = rq + 5;
    u8 *o = rs + 4;
    u32 done = 0, ack = 0, data;

    rs[0] = ID_DAP_TransferBlock;
    if (dap.port != 1 || count == 0) { put_le16(rs + 1, 0); rs[3] = 0; return 4; }
    probe_mode(PM_SWD, dap.hz);

    if (req & T_RnW) {
        if (req & T_APnDP) {
            ack = swd_retry(req, NULL);
            if (ack != ACK_OK) goto end;
        }
        while (count--) {
            u32 r = req;
            if (count == 0 && (req & T_APnDP)) r = DP_RDBUFF | T_RnW;
            ack = swd_retry(r, &data);
            if (ack != ACK_OK) goto end;
            put_le32(o, data); o += 4;
            done++;
        }
    } else {
        while (count--) {
            data = le32(p); p += 4;
            ack = swd_retry(req, &data);
            if (ack != ACK_OK) goto end;
            done++;
        }
        ack = swd_retry(DP_RDBUFF | T_RnW, NULL);
    }
end:
    put_le16(rs + 1, done);
    rs[3] = ack;
    return o - rs;
}

static u32 jtag_sequence(const u8 *rq, u8 *rs)
{
    u32 n = rq[1];
    const u8 *p = rq + 2;
    u8 *o = rs + 2;
    rs[0] = ID_DAP_JTAG_Sequence;
    probe_mode(PM_JTAG, dap.hz);
    while (n--) {
        u32 info = *p++;
        u32 bits = info & 0x3F;
        if (!bits) bits = 64;
        u32 nbytes = (bits + 7) / 8;
        u8 tms[8];
        memset(tms, (info & 0x40) ? 0xFF : 0x00, sizeof tms);
        if (info & 0x80) {
            probe_shift(PM_JTAG, bits, tms, p, NULL, o);
            o += nbytes;
        } else {
            probe_shift(PM_JTAG, bits, tms, p, NULL, NULL);
        }
        p += nbytes;
    }
    rs[1] = DAP_OK;
    return o - rs;
}

static u32 swd_sequence(const u8 *rq, u8 *rs)
{
    u32 n = rq[1];
    const u8 *p = rq + 2;
    u8 *o = rs + 2;
    rs[0] = ID_DAP_SWD_Sequence;
    probe_mode(PM_SWD, dap.hz);
    while (n--) {
        u32 info = *p++;
        u32 bits = info & 0x3F;
        if (!bits) bits = 64;
        u32 nbytes = (bits + 7) / 8;
        if (info & 0x80) {
            u8 zero[8] = {0};
            probe_shift(PM_SWD, bits, NULL, NULL, zero, o);
            o += nbytes;
        } else {
            probe_shift(PM_SWD, bits, p, NULL, NULL, NULL);
            p += nbytes;
        }
    }
    rs[1] = DAP_OK;
    return o - rs;
}

static u32 swj_sequence(const u8 *rq, u8 *rs)
{
    u32 bits = rq[1];
    if (!bits) bits = 256;
    // TMS/SWDIO с тактом; в JTAG TDI держим в 1
    if (dap.port == 2) {
        probe_mode(PM_JTAG, dap.hz);
        probe_shift(PM_JTAG, bits, rq + 2, NULL, NULL, NULL);
    } else {
        probe_mode(PM_SWD, dap.hz);
        probe_shift(PM_SWD, bits, rq + 2, NULL, NULL, NULL);
    }
    rs[0] = ID_DAP_SWJ_Sequence;
    rs[1] = DAP_OK;
    return 2;
}

// Разобрать один запрос; вернуть длину ответа (0 — ответа нет)
static u32 dap_command(const u8 *rq, u32 len, u8 *rs)
{
    switch (rq[0]) {
    case ID_DAP_Info:
        return info(rq, rs);
    case ID_DAP_HostStatus:
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_Connect: {
        u32 port = rq[1];
        if (port == 0) port = 1;
        if (port != 1 && port != 2) port = 0;
        dap.port = port;
        if (port) {
            probe_mode(port == 2 ? PM_JTAG : PM_SWD, dap.hz);
            probe_od(PIN_NRST, 1);
        }
        xil_printf("dap: подключение %s\r\n", port == 1 ? "SWD" : port == 2 ? "JTAG" : "—");
        rs[0] = rq[0]; rs[1] = port; return 2;
    }
    case ID_DAP_Disconnect:
        dap.port = 0;
        pins_release();
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_TransferConfigure:
        dap.idle_cycles = rq[1];
        dap.wait_retry  = le16(rq + 2);
        dap.match_retry = le16(rq + 4);
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_Transfer:
        return transfer(rq, len, rs);
    case ID_DAP_TransferBlock:
        return transfer_block(rq, rs);
    case ID_DAP_TransferAbort:
        return 0;
    case ID_DAP_WriteABORT: {
        u32 d = le32(rq + 2);
        rs[0] = rq[0];
        if (dap.port == 1) {
            probe_mode(PM_SWD, dap.hz);
            rs[1] = swd_transfer(0x00, &d) == ACK_OK ? DAP_OK : DAP_ERROR;
        } else {
            rs[1] = DAP_ERROR;
        }
        return 2;
    }
    case ID_DAP_Delay:
        probe_delay_us(le16(rq + 1));
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_ResetTarget:
        rs[0] = rq[0]; rs[1] = DAP_OK; rs[2] = 0; return 3;
    case ID_DAP_SWJ_Pins:
        return swj_pins(rq, rs);
    case ID_DAP_SWJ_Clock:
        dap.hz = probe_hz_actual(le32(rq + 1));
        if (dap.port) probe_mode(cur_mode(), dap.hz);
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_SWJ_Sequence:
        return swj_sequence(rq, rs);
    case ID_DAP_SWD_Configure:
        dap.turnaround = (rq[1] & 3) + 1;
        dap.data_phase = (rq[1] >> 2) & 1;
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_JTAG_Sequence:
        return jtag_sequence(rq, rs);
    case ID_DAP_JTAG_Configure:
        rs[0] = rq[0]; rs[1] = DAP_OK; return 2;
    case ID_DAP_JTAG_IDCODE:
        rs[0] = rq[0]; rs[1] = DAP_ERROR; memset(rs + 2, 0, 4); return 6;
    case ID_DAP_SWD_Sequence:
        return swd_sequence(rq, rs);
    default:
        rs[0] = 0xFF;
        return 1;
    }
}

static void process(tcpsrv_t *s)
{
    while (s->rxlen >= HDR) {
        const u8 *h = s->rx;
        if (le32(h) != DAP_SIG || h[6] != 1) {
            xil_printf("dap: неверный заголовок, сброс соединения\r\n");
            srv_close(s);
            return;
        }
        u32 len = le16(h + 4);
        if (len > DAP_PKT || len == 0) { srv_close(s); return; }
        if (s->rxlen < HDR + len) break;
        if (srv_space(s) < HDR + DAP_PKT) break;

        u32 rl = dap_command(h + HDR, len, out + HDR);
        if (rl) {
            put_le32(out, DAP_SIG);
            put_le16(out + 4, rl);
            out[6] = 2;
            out[7] = 0;
            srv_send(s, out, HDR + rl);
        }
        srv_consume(s, HDR + len);
    }
    srv_flush(s);
}

static void closed(tcpsrv_t *s)
{
    (void)s;
    if (dap.port) {
        dap.port = 0;
        pins_release();
    }
}

void svc_dap_init(void)
{
    srv.name = "dap";
    srv.port = 4441;
    srv.process = process;
    srv.closed = closed;
    srv_start(&srv);
}

void svc_dap_poll(void) { srv_poll(&srv); }
