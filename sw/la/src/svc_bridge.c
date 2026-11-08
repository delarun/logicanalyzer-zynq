// Двоичный мост I2C/SPI для s9bridge на ПК (/dev/i2c-N, /dev/spidevN.M), TCP 5561.
//
// Запрос:  u8 op, u8 0, u16 len (LE), данные[len]
// Ответ:   u8 status (0 OK, 1 NACK, 2 неверный запрос), u8 0, u16 len, данные[len]
//
// op 0x01 I2C_XFER:  u8 n; n × { u16 addr, u16 flags (как в linux/i2c.h), u16 len, [данные, если запись] }
//                    ответ — прочитанные байты всех чтений подряд
// op 0x02 I2C_SPEED: u32 Гц → ответ u32 фактическая
// op 0x10 SPI_XFER:  u32 Гц, u8 mode, u8 n; n × { u16 len, u8 flags (b0 есть tx, b1 нужен rx,
//                    b2 cs_change), [tx] } — CS активен на всё сообщение, cs_change даёт
//                    паузу CS между сегментами; ответ — rx всех сегментов с b1 подряд
// op 0x20 PING
#include <string.h>

#include "i2c.h"
#include "probe.h"
#include "services.h"
#include "tcpsrv.h"

#define MAXRESP 16384

static tcpsrv_t srv;
static u8 resp[4 + MAXRESP];

static u32 do_i2c(const u8 *p, u32 len, u8 *o, u32 *olen)
{
    if (len < 1) return 2;
    u32 n = p[0], pos = 1, ol = 0;
    int err = 0;
    for (u32 i = 0; i < n && !err; i++) {
        if (pos + 6 > len) return 2;
        u32 addr = le16(p + pos), flags = le16(p + pos + 2), mlen = le16(p + pos + 4);
        pos += 6;
        int rd = flags & I2C_M_RD;
        if (!rd && pos + mlen > len) return 2;
        if (rd && ol + mlen + 32 > MAXRESP) return 2;
        if (!(flags & I2C_M_NOSTART)) {
            i2c_start();
            int nack = i2c_wbyte((u8)((addr << 1) | (rd ? 1 : 0)));
            if (nack && !(flags & I2C_M_IGNORE_NAK)) { err = 1; break; }
        }
        if (rd) {
            if (flags & I2C_M_RECV_LEN) {
                u8 cnt = i2c_rbyte(1);
                if (cnt > 32) cnt = 32;
                o[ol++] = cnt;
                for (u32 k = 0; k < cnt; k++) o[ol++] = i2c_rbyte(k + 1 < cnt);
            } else {
                for (u32 k = 0; k < mlen; k++) o[ol++] = i2c_rbyte(k + 1 < mlen);
            }
        } else {
            for (u32 k = 0; k < mlen; k++) {
                if (i2c_wbyte(p[pos + k]) && !(flags & I2C_M_IGNORE_NAK)) { err = 1; break; }
            }
            pos += mlen;
        }
    }
    i2c_stop();
    *olen = ol;
    return err ? 1 : 0;
}

static void cs(int level)
{
    probe_gpio(1u << PIN_CS, level ? 1u << PIN_CS : 0, 1u << PIN_CS);
}

static u32 do_spi(const u8 *p, u32 len, u8 *o, u32 *olen)
{
    if (len < 6) return 2;
    u32 hz = le32(p), n = p[5], pos = 6, ol = 0;
    static u8 tmp[512];
    probe_mode(PM_SPI, probe_hz_actual(hz ? hz : 1000000));
    cs(0);
    for (u32 i = 0; i < n; i++) {
        if (pos + 3 > len) { cs(1); return 2; }
        u32 sl = le16(p + pos), fl = p[pos + 2];
        pos += 3;
        const u8 *tx = NULL;
        if (fl & 1) {
            if (pos + sl > len) { cs(1); return 2; }
            tx = p + pos;
            pos += sl;
        }
        if ((fl & 2) && ol + sl > MAXRESP) { cs(1); return 2; }
        for (u32 off = 0; off < sl; ) {
            u32 k = sl - off > sizeof tmp ? sizeof tmp : sl - off;
            for (u32 j = 0; j < k; j++) tmp[j] = tx ? rev8(tx[off + j]) : 0;
            if (fl & 2) {
                probe_shift(PM_SPI, k * 8, tmp, NULL, NULL, o + ol);
                for (u32 j = 0; j < k; j++) o[ol + j] = rev8(o[ol + j]);
                ol += k;
            } else {
                probe_shift(PM_SPI, k * 8, tmp, NULL, NULL, NULL);
            }
            off += k;
        }
        if ((fl & 4) && i + 1 < n) {      // cs_change между сегментами
            cs(1);
            probe_delay_us(1);
            cs(0);
        }
    }
    cs(1);
    *olen = ol;
    return 0;
}

static void process(tcpsrv_t *s)
{
    while (s->rxlen >= 4) {
        u32 len = le16(s->rx + 2);
        if (s->rxlen < 4 + len) break;
        if (srv_space(s) < 4 + MAXRESP) break;
        const u8 *p = s->rx + 4;
        u32 st = 2, ol = 0;
        switch (s->rx[0]) {
        case 0x01: st = do_i2c(p, len, resp + 4, &ol); break;
        case 0x02:
            if (len >= 4) { put_le32(resp + 4, i2c_speed(le32(p))); ol = 4; st = 0; }
            break;
        case 0x10: st = do_spi(p, len, resp + 4, &ol); break;
        case 0x20: st = 0; break;
        default: break;
        }
        resp[0] = st; resp[1] = 0;
        put_le16(resp + 2, ol);
        srv_send(s, resp, 4 + ol);
        srv_consume(s, 4 + len);
    }
    srv_flush(s);
}

void svc_bridge_init(void)
{
    srv.name = "bridge";
    srv.port = 5561;
    srv.process = process;
    srv_start(&srv);
}

void svc_bridge_poll(void) { srv_poll(&srv); }
