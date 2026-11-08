// serprog (протокол flashrom) поверх TCP, порт 5557: SPI-флешки через линии FAN4..6 + SCL1.
//   flashrom -p serprog:ip=<адрес>:5557,spispeed=8M -r dump.bin
#include <string.h>

#include "probe.h"
#include "services.h"
#include "tcpsrv.h"

#define S_ACK 0x06
#define S_NAK 0x15

#define S_CMD_NOP          0x00
#define S_CMD_Q_IFACE      0x01
#define S_CMD_Q_CMDMAP     0x02
#define S_CMD_Q_PGMNAME    0x03
#define S_CMD_Q_SERBUF     0x04
#define S_CMD_Q_BUSTYPE    0x05
#define S_CMD_Q_WRNMAXLEN  0x08
#define S_CMD_SYNCNOP      0x10
#define S_CMD_Q_RDNMAXLEN  0x11
#define S_CMD_S_BUSTYPE    0x12
#define S_CMD_O_SPIOP      0x13
#define S_CMD_S_SPI_FREQ   0x14
#define S_CMD_S_PIN_STATE  0x15
#define S_CMD_S_SPI_CS     0x16

#define MAXLEN 4096         // за одну операцию: запись и чтение

static tcpsrv_t srv;
static u32 spi_hz = 8000000;
static u8 buf[1 + MAXLEN];

static void cs(int level)
{
    probe_gpio(1u << PIN_CS, level ? 1u << PIN_CS : 0, 1u << PIN_CS);
}

static void spi_out(const u8 *d, u32 n)
{
    static u8 tmp[256];
    while (n) {
        u32 k = n > sizeof tmp ? sizeof tmp : n;
        for (u32 i = 0; i < k; i++) tmp[i] = rev8(d[i]);   // SPI — старшим битом вперёд
        probe_shift(PM_SPI, k * 8, tmp, NULL, NULL, NULL);
        d += k; n -= k;
    }
}

static void spi_in(u8 *d, u32 n)
{
    static const u8 ones[256] = {[0 ... 255] = 0xFF};
    while (n) {
        u32 k = n > sizeof ones ? sizeof ones : n;
        probe_shift(PM_SPI, k * 8, ones, NULL, NULL, d);
        for (u32 i = 0; i < k; i++) d[i] = rev8(d[i]);
        d += k; n -= k;
    }
}

static void reply(tcpsrv_t *s, const u8 *d, u32 n, u32 used)
{
    srv_send(s, d, n);
    srv_consume(s, used);
}

static void process(tcpsrv_t *s)
{
    for (;;) {
        const u8 *rx = s->rx;
        u32 len = s->rxlen;
        if (!len) break;
        if (srv_space(s) < 1 + MAXLEN) break;
        u8 r[40];

        switch (rx[0]) {
        case S_CMD_NOP:
            r[0] = S_ACK; reply(s, r, 1, 1); break;
        case S_CMD_Q_IFACE:
            r[0] = S_ACK; r[1] = 1; r[2] = 0; reply(s, r, 3, 1); break;
        case S_CMD_Q_CMDMAP: {
            u32 cmds[] = {S_CMD_NOP, S_CMD_Q_IFACE, S_CMD_Q_CMDMAP, S_CMD_Q_PGMNAME, S_CMD_Q_SERBUF,
                          S_CMD_Q_BUSTYPE, S_CMD_Q_WRNMAXLEN, S_CMD_SYNCNOP, S_CMD_Q_RDNMAXLEN,
                          S_CMD_S_BUSTYPE, S_CMD_O_SPIOP, S_CMD_S_SPI_FREQ, S_CMD_S_PIN_STATE,
                          S_CMD_S_SPI_CS};
            memset(r, 0, 33);
            r[0] = S_ACK;
            for (u32 i = 0; i < sizeof cmds / sizeof cmds[0]; i++) r[1 + cmds[i] / 8] |= 1u << (cmds[i] % 8);
            reply(s, r, 33, 1);
            break;
        }
        case S_CMD_Q_PGMNAME:
            memset(r, 0, 17);
            r[0] = S_ACK;
            memcpy(r + 1, "S9probe", 7);
            reply(s, r, 17, 1);
            break;
        case S_CMD_Q_SERBUF:
            r[0] = S_ACK; put_le16(r + 1, 0xFFFF); reply(s, r, 3, 1); break;
        case S_CMD_Q_BUSTYPE:
            r[0] = S_ACK; r[1] = 0x08; reply(s, r, 2, 1); break;     // только SPI
        case S_CMD_Q_WRNMAXLEN:
        case S_CMD_Q_RDNMAXLEN:
            r[0] = S_ACK; r[1] = MAXLEN & 0xFF; r[2] = (MAXLEN >> 8) & 0xFF; r[3] = MAXLEN >> 16;
            reply(s, r, 4, 1);
            break;
        case S_CMD_SYNCNOP:
            r[0] = S_NAK; r[1] = S_ACK; reply(s, r, 2, 1); break;
        case S_CMD_S_BUSTYPE:
            if (len < 2) goto wait;
            r[0] = (rx[1] & 0x08) ? S_ACK : S_NAK;
            reply(s, r, 1, 2);
            break;
        case S_CMD_S_SPI_FREQ: {
            if (len < 5) goto wait;
            u32 f = le32(rx + 1);
            if (!f) { r[0] = S_NAK; reply(s, r, 1, 5); break; }
            spi_hz = probe_hz_actual(f > 16000000 ? 16000000 : f);
            r[0] = S_ACK; put_le32(r + 1, spi_hz);
            reply(s, r, 5, 5);
            break;
        }
        case S_CMD_S_PIN_STATE:
            if (len < 2) goto wait;
            if (rx[1]) { cs(1); probe_mode(PM_SPI, spi_hz); }
            else { probe_mode(PM_NONE, 0); probe_gpio(1u << PIN_CS, 0, 0); }
            r[0] = S_ACK; reply(s, r, 1, 2);
            break;
        case S_CMD_S_SPI_CS:
            if (len < 2) goto wait;
            r[0] = rx[1] == 0 ? S_ACK : S_NAK; reply(s, r, 1, 2);
            break;
        case S_CMD_O_SPIOP: {
            if (len < 7) goto wait;
            u32 slen = le24(rx + 1), rlen = le24(rx + 4);
            if (slen > MAXLEN || rlen > MAXLEN) { r[0] = S_NAK; reply(s, r, 1, 7); break; }
            if (len < 7 + slen) goto wait;
            probe_mode(PM_SPI, spi_hz);
            cs(0);
            spi_out(rx + 7, slen);
            spi_in(buf + 1, rlen);
            cs(1);
            buf[0] = S_ACK;
            reply(s, buf, 1 + rlen, 7 + slen);
            break;
        }
        default:
            r[0] = S_NAK; reply(s, r, 1, 1); break;
        }
    }
wait:
    srv_flush(s);
}

void svc_serprog_init(void)
{
    srv.name = "serprog";
    srv.port = 5557;
    srv.process = process;
    srv_start(&srv);
}

void svc_serprog_poll(void) { srv_poll(&srv); }
