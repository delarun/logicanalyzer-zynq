// s9bridge — интерфейсы S9 probe как локальные устройства Linux:
//   /dev/ttyETH0     — UART (PTY ↔ RFC2217, скорость берётся из настроек терминала)
//   /dev/i2c-N       — I2C (CUSE, ioctl как у i2c-dev: I2C_RDWR, I2C_SMBUS, I2C_SLAVE, ...)
//   /dev/spidevN.M   — SPI (CUSE, ioctl как у spidev: SPI_IOC_MESSAGE, режим, частота)
//
//   sudo s9bridge                                    # настройки из /etc/s9bridge.conf
//   sudo s9bridge 192.168.10.225 --i2c 3 --no-spi --tty /dev/ttyS9
//
// К плате обращается только пока открыт хотя бы один из файлов в /dev; в остальное
// время спит в ядре (CUSE — в read() на /dev/cuse, UART — в poll() на inotify) и не ест CPU.
// Требует root (или права на /dev/cuse) и модуль ядра cuse.
#define FUSE_USE_VERSION 31
#define _GNU_SOURCE

#include <cuse_lowlevel.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse_lowlevel.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static const char *host;
static const char *tty_path = "/dev/ttyETH0";
static const char *config_path = "/etc/s9bridge.conf";
static int i2c_bus = 10, spi_bus = 10, spi_cs = 0;
static int use_tty = 1, use_i2c = 1, use_spi = 1;
static volatile sig_atomic_t stop;

#define PORT_RFC2217 "5558"
#define PORT_BRIDGE  "5561"

static void logm(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logm(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("s9bridge: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int tcp_connect(const char *port)
{
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *res, *r;
    if (getaddrinfo(host, port, &hints, &res)) return -1;
    int fd = -1;
    for (r = res; r; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) continue;
        if (!connect(fd, r->ai_addr, r->ai_addrlen)) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

static int write_all(int fd, const void *d, size_t n)
{
    const uint8_t *p = d;
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return -1; }
        p += k; n -= k;
    }
    return 0;
}

static int read_all(int fd, void *d, size_t n)
{
    uint8_t *p = d;
    while (n) {
        ssize_t k = recv(fd, p, n, 0);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return -1; }
        p += k; n -= k;
    }
    return 0;
}

// =====================================================================
// Мост I2C/SPI (TCP 5561), общий для обоих устройств
// =====================================================================
static pthread_mutex_t br_lock = PTHREAD_MUTEX_INITIALIZER;
static int br_fd = -1;
static int br_users;          // открытых файлов /dev/i2c-N и /dev/spidevN.M

// Соединение с S9 живёт, пока открыт хотя бы один файл устройства
static void bridge_user(int delta)
{
    pthread_mutex_lock(&br_lock);
    br_users += delta;
    if (br_users <= 0) {
        br_users = 0;
        if (br_fd >= 0) { close(br_fd); br_fd = -1; }
    }
    pthread_mutex_unlock(&br_lock);
}

// Возвращает статус S9 (0 OK, 1 NACK, 2 ошибка запроса) или -1 при обрыве связи
static int bridge(uint8_t op, const uint8_t *req, uint16_t len, uint8_t *resp, uint16_t *rlen)
{
    pthread_mutex_lock(&br_lock);
    int st = -1;
    for (int attempt = 0; attempt < 2 && st < 0; attempt++) {
        if (br_fd < 0 && (br_fd = tcp_connect(PORT_BRIDGE)) < 0) break;
        uint8_t h[4] = {op, 0, (uint8_t)len, (uint8_t)(len >> 8)};
        uint8_t rh[4];
        if (write_all(br_fd, h, 4) || (len && write_all(br_fd, req, len)) || read_all(br_fd, rh, 4)) {
            close(br_fd);
            br_fd = -1;
            continue;
        }
        uint16_t n = rh[2] | rh[3] << 8;
        uint8_t dummy[256];
        if (resp) {
            if (read_all(br_fd, resp, n)) { close(br_fd); br_fd = -1; continue; }
        } else {
            for (uint16_t left = n; left; ) {
                uint16_t k = left > sizeof dummy ? sizeof dummy : left;
                if (read_all(br_fd, dummy, k)) break;
                left -= k;
            }
        }
        if (rlen) *rlen = n;
        st = rh[0];
    }
    pthread_mutex_unlock(&br_lock);
    return st;
}

// =====================================================================
// I2C
// =====================================================================
struct i2c_msg_lite { uint16_t addr, flags, len; const uint8_t *wbuf; uint8_t *rbuf; };

// Выполнить сообщения; вернуть 0 или -errno
static int i2c_do(struct i2c_msg_lite *m, int n)
{
    static uint8_t req[8192], resp[8192];
    uint32_t pos = 1, rtotal = 0;
    if (n > 42) return -EINVAL;
    req[0] = n;
    for (int i = 0; i < n; i++) {
        if (pos + 6 + (m[i].flags & I2C_M_RD ? 0 : m[i].len) > sizeof req) return -EINVAL;
        req[pos++] = m[i].addr; req[pos++] = m[i].addr >> 8;
        req[pos++] = m[i].flags; req[pos++] = m[i].flags >> 8;
        req[pos++] = m[i].len; req[pos++] = m[i].len >> 8;
        if (!(m[i].flags & I2C_M_RD)) {
            memcpy(req + pos, m[i].wbuf, m[i].len);
            pos += m[i].len;
        } else {
            rtotal += m[i].len;
        }
    }
    if (rtotal > sizeof resp) return -EINVAL;
    uint16_t rl = 0;
    int st = bridge(0x01, req, pos, resp, &rl);
    if (st < 0) return -EIO;
    if (st == 1) return -ENXIO;
    if (st) return -EINVAL;
    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        if (!(m[i].flags & I2C_M_RD)) continue;
        uint32_t k = m[i].len;
        if (m[i].flags & I2C_M_RECV_LEN) k = off < rl ? resp[off] + 1 : 0;
        if (off + k > rl) k = rl > off ? rl - off : 0;
        memcpy(m[i].rbuf, resp + off, k);
        off += k;
    }
    return 0;
}

struct i2c_fh { uint16_t addr; };

static void i2c_open(fuse_req_t req, struct fuse_file_info *fi)
{
    bridge_user(+1);
    struct i2c_fh *fh = calloc(1, sizeof *fh);
    fi->fh = (uintptr_t)fh;
    fi->direct_io = 1;
    fi->nonseekable = 1;
    fuse_reply_open(req, fi);
}

static void i2c_release(fuse_req_t req, struct fuse_file_info *fi)
{
    free((void *)(uintptr_t)fi->fh);
    bridge_user(-1);
    fuse_reply_err(req, 0);
}

static void i2c_read(fuse_req_t req, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)off;
    struct i2c_fh *fh = (void *)(uintptr_t)fi->fh;
    uint8_t buf[8192];
    if (size > sizeof buf) size = sizeof buf;
    struct i2c_msg_lite m = {fh->addr, I2C_M_RD, (uint16_t)size, NULL, buf};
    int r = i2c_do(&m, 1);
    if (r) fuse_reply_err(req, -r); else fuse_reply_buf(req, (char *)buf, size);
}

static void i2c_write(fuse_req_t req, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)off;
    struct i2c_fh *fh = (void *)(uintptr_t)fi->fh;
    if (size > 8000) size = 8000;
    struct i2c_msg_lite m = {fh->addr, 0, (uint16_t)size, (const uint8_t *)buf, NULL};
    int r = i2c_do(&m, 1);
    if (r) fuse_reply_err(req, -r); else fuse_reply_write(req, size);
}

#define FUNCS (I2C_FUNC_I2C | I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA | \
               I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_PROC_CALL | I2C_FUNC_SMBUS_BLOCK_DATA | \
               I2C_FUNC_SMBUS_I2C_BLOCK)

static int smbus(uint16_t addr, struct i2c_smbus_ioctl_data *a, union i2c_smbus_data *d)
{
    uint8_t cmd = a->command, w[40];
    int rd = a->read_write == I2C_SMBUS_READ;
    struct i2c_msg_lite m[2];
    switch (a->size) {
    case I2C_SMBUS_QUICK:
        m[0] = (struct i2c_msg_lite){addr, (uint16_t)(rd ? I2C_M_RD : 0), 0, w, w};
        return i2c_do(m, 1);
    case I2C_SMBUS_BYTE:
        if (rd) { m[0] = (struct i2c_msg_lite){addr, I2C_M_RD, 1, NULL, &d->byte}; }
        else    { w[0] = cmd; m[0] = (struct i2c_msg_lite){addr, 0, 1, w, NULL}; }
        return i2c_do(m, 1);
    case I2C_SMBUS_BYTE_DATA:
        w[0] = cmd;
        if (rd) {
            m[0] = (struct i2c_msg_lite){addr, 0, 1, w, NULL};
            m[1] = (struct i2c_msg_lite){addr, I2C_M_RD, 1, NULL, &d->byte};
            return i2c_do(m, 2);
        }
        w[1] = d->byte;
        m[0] = (struct i2c_msg_lite){addr, 0, 2, w, NULL};
        return i2c_do(m, 1);
    case I2C_SMBUS_WORD_DATA:
    case I2C_SMBUS_PROC_CALL: {
        uint8_t r[2];
        w[0] = cmd;
        if (rd && a->size == I2C_SMBUS_WORD_DATA) {
            m[0] = (struct i2c_msg_lite){addr, 0, 1, w, NULL};
            m[1] = (struct i2c_msg_lite){addr, I2C_M_RD, 2, NULL, r};
            int e = i2c_do(m, 2);
            d->word = r[0] | r[1] << 8;
            return e;
        }
        w[1] = d->word; w[2] = d->word >> 8;
        m[0] = (struct i2c_msg_lite){addr, 0, 3, w, NULL};
        if (a->size == I2C_SMBUS_WORD_DATA) return i2c_do(m, 1);
        m[1] = (struct i2c_msg_lite){addr, I2C_M_RD, 2, NULL, r};
        int e = i2c_do(m, 2);
        d->word = r[0] | r[1] << 8;
        return e;
    }
    case I2C_SMBUS_BLOCK_DATA:
        w[0] = cmd;
        if (rd) {
            m[0] = (struct i2c_msg_lite){addr, 0, 1, w, NULL};
            m[1] = (struct i2c_msg_lite){addr, I2C_M_RD | I2C_M_RECV_LEN, 33, NULL, d->block};
            return i2c_do(m, 2);
        }
        if (d->block[0] > 32) return -EINVAL;
        memcpy(w + 1, d->block, d->block[0] + 1);
        m[0] = (struct i2c_msg_lite){addr, 0, (uint16_t)(d->block[0] + 2), w, NULL};
        return i2c_do(m, 1);
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
    case I2C_SMBUS_I2C_BLOCK_DATA: {
        uint8_t n = d->block[0];
        if (n > 32) n = 32;
        w[0] = cmd;
        if (rd) {
            m[0] = (struct i2c_msg_lite){addr, 0, 1, w, NULL};
            m[1] = (struct i2c_msg_lite){addr, I2C_M_RD, n, NULL, d->block + 1};
            return i2c_do(m, 2);
        }
        memcpy(w + 1, d->block + 1, n);
        m[0] = (struct i2c_msg_lite){addr, 0, (uint16_t)(n + 1), w, NULL};
        return i2c_do(m, 1);
    }
    default:
        return -EOPNOTSUPP;
    }
}

static void i2c_ioctl(fuse_req_t req, int cmd, void *arg, struct fuse_file_info *fi,
                      unsigned flags, const void *in, size_t in_sz, size_t out_sz)
{
    struct i2c_fh *fh = (void *)(uintptr_t)fi->fh;
    if (flags & FUSE_IOCTL_COMPAT) { fuse_reply_err(req, ENOSYS); return; }

    switch ((unsigned)cmd) {
    case I2C_SLAVE:
    case I2C_SLAVE_FORCE:
        if ((uintptr_t)arg > 0x7F) { fuse_reply_err(req, EINVAL); return; }
        fh->addr = (uint16_t)(uintptr_t)arg;
        fuse_reply_ioctl(req, 0, NULL, 0);
        return;
    case I2C_TENBIT:
        fuse_reply_ioctl(req, arg ? -1 : 0, NULL, 0);
        return;
    case I2C_PEC:
    case I2C_RETRIES:
    case I2C_TIMEOUT:
        fuse_reply_ioctl(req, 0, NULL, 0);
        return;
    case I2C_FUNCS: {
        unsigned long f = FUNCS;
        if (!out_sz) {
            struct iovec iov = {arg, sizeof f};
            fuse_reply_ioctl_retry(req, NULL, 0, &iov, 1);
        } else {
            fuse_reply_ioctl(req, 0, &f, sizeof f);
        }
        return;
    }
    case I2C_RDWR: {
        struct iovec iin[1 + 1 + I2C_RDWR_IOCTL_MAX_MSGS], iout[I2C_RDWR_IOCTL_MAX_MSGS];
        if (in_sz < sizeof(struct i2c_rdwr_ioctl_data)) {
            iin[0] = (struct iovec){arg, sizeof(struct i2c_rdwr_ioctl_data)};
            fuse_reply_ioctl_retry(req, iin, 1, NULL, 0);
            return;
        }
        const struct i2c_rdwr_ioctl_data *rw = in;
        uint32_t n = rw->nmsgs;
        if (n == 0 || n > I2C_RDWR_IOCTL_MAX_MSGS) { fuse_reply_err(req, EINVAL); return; }
        size_t need = sizeof *rw + n * sizeof(struct i2c_msg);
        if (in_sz < need) {
            iin[0] = (struct iovec){arg, sizeof *rw};
            iin[1] = (struct iovec){rw->msgs, n * sizeof(struct i2c_msg)};
            fuse_reply_ioctl_retry(req, iin, 2, NULL, 0);
            return;
        }
        const struct i2c_msg *um = (const void *)((const uint8_t *)in + sizeof *rw);
        size_t wtot = 0, rtot = 0;
        int ni = 2, no = 0;
        iin[0] = (struct iovec){arg, sizeof *rw};
        iin[1] = (struct iovec){rw->msgs, n * sizeof(struct i2c_msg)};
        for (uint32_t i = 0; i < n; i++) {
            if (um[i].flags & I2C_M_RD) { iout[no++] = (struct iovec){um[i].buf, um[i].len}; rtot += um[i].len; }
            else if (um[i].len) { iin[ni++] = (struct iovec){um[i].buf, um[i].len}; wtot += um[i].len; }
        }
        if (in_sz < need + wtot || (rtot && out_sz < rtot)) {
            fuse_reply_ioctl_retry(req, iin, ni, no ? iout : NULL, no);
            return;
        }
        // данные на месте: выполняем
        struct i2c_msg_lite m[I2C_RDWR_IOCTL_MAX_MSGS];
        uint8_t *rbuf = malloc(rtot ? rtot : 1);
        const uint8_t *wp = (const uint8_t *)in + need;
        size_t ro = 0;
        for (uint32_t i = 0; i < n; i++) {
            m[i].addr = um[i].addr; m[i].flags = um[i].flags; m[i].len = um[i].len;
            if (um[i].flags & I2C_M_RD) { m[i].rbuf = rbuf + ro; m[i].wbuf = NULL; ro += um[i].len; }
            else { m[i].wbuf = wp; m[i].rbuf = NULL; wp += um[i].len; }
        }
        int r = i2c_do(m, (int)n);
        if (r) fuse_reply_err(req, -r);
        else fuse_reply_ioctl(req, (int)n, rbuf, rtot);
        free(rbuf);
        return;
    }
    case I2C_SMBUS: {
        struct iovec iin[2], iout[1];
        if (in_sz < sizeof(struct i2c_smbus_ioctl_data)) {
            iin[0] = (struct iovec){arg, sizeof(struct i2c_smbus_ioctl_data)};
            fuse_reply_ioctl_retry(req, iin, 1, NULL, 0);
            return;
        }
        struct i2c_smbus_ioctl_data a = *(const struct i2c_smbus_ioctl_data *)in;
        union i2c_smbus_data d;
        memset(&d, 0, sizeof d);
        if (a.data) {
            if (in_sz < sizeof a + sizeof d || out_sz < sizeof d) {
                iin[0] = (struct iovec){arg, sizeof a};
                iin[1] = (struct iovec){a.data, sizeof d};
                iout[0] = (struct iovec){a.data, sizeof d};
                fuse_reply_ioctl_retry(req, iin, 2, iout, 1);
                return;
            }
            memcpy(&d, (const uint8_t *)in + sizeof a, sizeof d);
        }
        int r = smbus(fh->addr, &a, &d);
        if (r) fuse_reply_err(req, -r);
        else fuse_reply_ioctl(req, 0, a.data ? &d : NULL, a.data ? sizeof d : 0);
        return;
    }
    default:
        fuse_reply_err(req, ENOTTY);
    }
}

static const struct cuse_lowlevel_ops i2c_ops = {
    .open = i2c_open, .release = i2c_release, .read = i2c_read, .write = i2c_write, .ioctl = i2c_ioctl,
};

// =====================================================================
// SPI
// =====================================================================
struct spi_fh { uint32_t mode, speed; uint8_t bits, lsb; };

static uint8_t rev8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

struct seg { const uint8_t *tx; uint8_t *rx; uint32_t len; int cs_change; };

static int spi_do(struct spi_fh *fh, struct seg *s, int n, uint32_t speed)
{
    static uint8_t req[20000], resp[17000];
    uint32_t pos = 6, rtot = 0;
    if (!speed) speed = fh->speed;
    req[0] = speed; req[1] = speed >> 8; req[2] = speed >> 16; req[3] = speed >> 24;
    req[4] = fh->mode & 3;
    req[5] = n;
    for (int i = 0; i < n; i++) {
        uint8_t fl = (s[i].tx ? 1 : 0) | (s[i].rx ? 2 : 0) | (s[i].cs_change ? 4 : 0);
        if (pos + 3 + (s[i].tx ? s[i].len : 0) > sizeof req || s[i].len > 0xFFFF) return -EMSGSIZE;
        req[pos++] = s[i].len; req[pos++] = s[i].len >> 8; req[pos++] = fl;
        if (s[i].tx) {
            for (uint32_t k = 0; k < s[i].len; k++) req[pos + k] = fh->lsb ? rev8(s[i].tx[k]) : s[i].tx[k];
            pos += s[i].len;
        }
        if (s[i].rx) rtot += s[i].len;
    }
    if (rtot > 16384 || pos > 0xFFFF) return -EMSGSIZE;
    uint16_t rl = 0;
    int st = bridge(0x10, req, pos, resp, &rl);
    if (st < 0) return -EIO;
    if (st) return -EINVAL;
    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        if (!s[i].rx) continue;
        for (uint32_t k = 0; k < s[i].len && off < rl; k++, off++)
            s[i].rx[k] = fh->lsb ? rev8(resp[off]) : resp[off];
    }
    return 0;
}

static void spi_open(fuse_req_t req, struct fuse_file_info *fi)
{
    bridge_user(+1);
    struct spi_fh *fh = calloc(1, sizeof *fh);
    fh->speed = 1000000;
    fh->bits = 8;
    fi->fh = (uintptr_t)fh;
    fi->direct_io = 1;
    fi->nonseekable = 1;
    fuse_reply_open(req, fi);
}

static void spi_read(fuse_req_t req, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)off;
    uint8_t buf[16384];
    if (size > sizeof buf) size = sizeof buf;
    struct seg s = {NULL, buf, (uint32_t)size, 0};
    int r = spi_do((void *)(uintptr_t)fi->fh, &s, 1, 0);
    if (r) fuse_reply_err(req, -r); else fuse_reply_buf(req, (char *)buf, size);
}

static void spi_write(fuse_req_t req, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)off;
    if (size > 16384) size = 16384;
    struct seg s = {(const uint8_t *)buf, NULL, (uint32_t)size, 0};
    int r = spi_do((void *)(uintptr_t)fi->fh, &s, 1, 0);
    if (r) fuse_reply_err(req, -r); else fuse_reply_write(req, size);
}

static void reply_value(fuse_req_t req, void *arg, size_t out_sz, const void *v, size_t n)
{
    if (out_sz < n) {
        struct iovec iov = {arg, n};
        fuse_reply_ioctl_retry(req, NULL, 0, &iov, 1);
    } else {
        fuse_reply_ioctl(req, 0, v, n);
    }
}

static int get_value(fuse_req_t req, void *arg, const void *in, size_t in_sz, void *v, size_t n)
{
    if (in_sz < n) {
        struct iovec iov = {arg, n};
        fuse_reply_ioctl_retry(req, &iov, 1, NULL, 0);
        return 0;
    }
    memcpy(v, in, n);
    return 1;
}

static void spi_ioctl(fuse_req_t req, int cmd, void *arg, struct fuse_file_info *fi,
                      unsigned flags, const void *in, size_t in_sz, size_t out_sz)
{
    struct spi_fh *fh = (void *)(uintptr_t)fi->fh;
    unsigned c = (unsigned)cmd;
    if (flags & FUSE_IOCTL_COMPAT) { fuse_reply_err(req, ENOSYS); return; }

    if (_IOC_TYPE(c) == SPI_IOC_MAGIC && _IOC_NR(c) == 0 && _IOC_DIR(c) == _IOC_WRITE) {
        uint32_t n = _IOC_SIZE(c) / sizeof(struct spi_ioc_transfer);
        if (n == 0 || n > 64) { fuse_reply_err(req, EINVAL); return; }
        size_t asz = n * sizeof(struct spi_ioc_transfer);
        struct iovec iin[1 + 64], iout[64];
        if (in_sz < asz) {
            iin[0] = (struct iovec){arg, asz};
            fuse_reply_ioctl_retry(req, iin, 1, NULL, 0);
            return;
        }
        const struct spi_ioc_transfer *t = in;
        size_t wtot = 0, rtot = 0;
        int ni = 1, no = 0;
        iin[0] = (struct iovec){arg, asz};
        for (uint32_t i = 0; i < n; i++) {
            if (t[i].tx_buf) { iin[ni++] = (struct iovec){(void *)(uintptr_t)t[i].tx_buf, t[i].len}; wtot += t[i].len; }
            if (t[i].rx_buf) { iout[no++] = (struct iovec){(void *)(uintptr_t)t[i].rx_buf, t[i].len}; rtot += t[i].len; }
        }
        if (in_sz < asz + wtot || out_sz < rtot) {
            fuse_reply_ioctl_retry(req, iin, ni, no ? iout : NULL, no);
            return;
        }
        struct seg s[64];
        uint8_t *rbuf = malloc(rtot ? rtot : 1);
        const uint8_t *wp = (const uint8_t *)in + asz;
        size_t ro = 0, total = 0;
        uint32_t speed = 0;
        for (uint32_t i = 0; i < n; i++) {
            s[i].len = t[i].len;
            s[i].tx = t[i].tx_buf ? wp : NULL;
            if (t[i].tx_buf) wp += t[i].len;
            s[i].rx = t[i].rx_buf ? rbuf + ro : NULL;
            if (t[i].rx_buf) ro += t[i].len;
            s[i].cs_change = t[i].cs_change;
            if (t[i].speed_hz) speed = t[i].speed_hz;
            total += t[i].len;
        }
        int r = spi_do(fh, s, (int)n, speed);
        if (r) fuse_reply_err(req, -r);
        else fuse_reply_ioctl(req, (int)total, rbuf, rtot);
        free(rbuf);
        return;
    }

    uint8_t v8;
    uint32_t v32;
    switch (c) {
    case SPI_IOC_RD_MODE:          v8 = fh->mode; reply_value(req, arg, out_sz, &v8, 1); return;
    case SPI_IOC_RD_MODE32:        reply_value(req, arg, out_sz, &fh->mode, 4); return;
    case SPI_IOC_RD_LSB_FIRST:     v8 = fh->lsb; reply_value(req, arg, out_sz, &v8, 1); return;
    case SPI_IOC_RD_BITS_PER_WORD: v8 = fh->bits; reply_value(req, arg, out_sz, &v8, 1); return;
    case SPI_IOC_RD_MAX_SPEED_HZ:  reply_value(req, arg, out_sz, &fh->speed, 4); return;
    case SPI_IOC_WR_MODE:
        if (!get_value(req, arg, in, in_sz, &v8, 1)) return;
        fh->mode = (fh->mode & ~0xFFu) | v8;
        fuse_reply_ioctl(req, 0, NULL, 0); return;
    case SPI_IOC_WR_MODE32:
        if (!get_value(req, arg, in, in_sz, &v32, 4)) return;
        fh->mode = v32;
        fuse_reply_ioctl(req, 0, NULL, 0); return;
    case SPI_IOC_WR_LSB_FIRST:
        if (!get_value(req, arg, in, in_sz, &v8, 1)) return;
        fh->lsb = v8 ? 1 : 0;
        fuse_reply_ioctl(req, 0, NULL, 0); return;
    case SPI_IOC_WR_BITS_PER_WORD:
        if (!get_value(req, arg, in, in_sz, &v8, 1)) return;
        if (v8 != 0 && v8 != 8) { fuse_reply_err(req, EINVAL); return; }
        fuse_reply_ioctl(req, 0, NULL, 0); return;
    case SPI_IOC_WR_MAX_SPEED_HZ:
        if (!get_value(req, arg, in, in_sz, &v32, 4)) return;
        if (!v32) { fuse_reply_err(req, EINVAL); return; }
        fh->speed = v32 > 16000000 ? 16000000 : v32;
        fuse_reply_ioctl(req, 0, NULL, 0); return;
    default:
        fuse_reply_err(req, ENOTTY);
    }
}

static const struct cuse_lowlevel_ops spi_ops = {
    .open = spi_open, .release = i2c_release, .read = spi_read, .write = spi_write, .ioctl = spi_ioctl,
};

// =====================================================================
// CUSE: запуск устройства в отдельном потоке
// =====================================================================
struct cuse_dev { char name[64]; const struct cuse_lowlevel_ops *ops; struct fuse_session *se; };

static void *cuse_thread(void *p)
{
    struct cuse_dev *d = p;
    fuse_session_loop(d->se);
    return NULL;
}

static int cuse_start(struct cuse_dev *d)
{
    char devarg[96];
    snprintf(devarg, sizeof devarg, "DEVNAME=%s", d->name);
    const char *dev_info_argv[] = {devarg};
    struct cuse_info ci = {.dev_info_argc = 1, .dev_info_argv = dev_info_argv, .flags = CUSE_UNRESTRICTED_IOCTL};
    char *argv[] = {"s9bridge", "-f", "-s", NULL};
    int mt;
    d->se = cuse_lowlevel_setup(3, argv, &ci, d->ops, &mt, NULL);
    if (!d->se) {
        logm("не удалось создать /dev/%s (нужен root и модуль cuse: modprobe cuse)", d->name);
        return -1;
    }
    pthread_t th;
    pthread_create(&th, NULL, cuse_thread, d);
    pthread_detach(th);
    logm("/dev/%s готово", d->name);
    return 0;
}

// =====================================================================
// UART: PTY ↔ RFC2217
// =====================================================================
#define IAC 255
#define SB 250
#define SE 240
#define WILL 251
#define DO 253
#define COMPORT 44

static speed_t speeds[] = {B50, B75, B110, B134, B150, B200, B300, B600, B1200, B1800, B2400, B4800,
    B9600, B19200, B38400, B57600, B115200, B230400, B460800, B500000, B576000, B921600, B1000000,
    B1152000, B1500000, B2000000, B2500000, B3000000};
static unsigned bauds[] = {50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200,
    38400, 57600, 115200, 230400, 460800, 500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
    2500000, 3000000};

static unsigned term_baud(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t)) return 0;
    speed_t s = cfgetospeed(&t);
    for (size_t i = 0; i < sizeof speeds / sizeof speeds[0]; i++)
        if (speeds[i] == s) return bauds[i];
    return 0;
}

static int send_baud(int sock, unsigned b)
{
    uint8_t v[4] = {b >> 24, b >> 16, b >> 8, b}, m[16];
    int k = 0;
    m[k++] = IAC; m[k++] = SB; m[k++] = COMPORT; m[k++] = 1;
    for (int i = 0; i < 4; i++) { m[k++] = v[i]; if (v[i] == IAC) m[k++] = IAC; }
    m[k++] = IAC; m[k++] = SE;
    return write_all(sock, m, k);
}

// Клиенты открывают/закрывают pts — узнаём через inotify, без опроса.
// Пока клиентов нет: соединения с S9 нет, процесс спит в poll() без таймаута.
static void uart_loop(void)
{
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL)) { logm("openpty: %s", strerror(errno)); return; }
    struct termios t;
    tcgetattr(slave, &t);
    cfmakeraw(&t);
    cfsetspeed(&t, B115200);
    tcsetattr(slave, TCSANOW, &t);
    const char *pts = ptsname(master);
    char ptsbuf[64];
    snprintf(ptsbuf, sizeof ptsbuf, "%s", pts);
    unlink(tty_path);
    if (symlink(ptsbuf, tty_path)) { logm("symlink %s → %s: %s", tty_path, ptsbuf, strerror(errno)); return; }
    chmod(ptsbuf, 0666);
    close(slave);              // сами не держим: иначе не узнать, что клиентов нет

    int ino = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ino < 0 || inotify_add_watch(ino, ptsbuf, IN_OPEN | IN_CLOSE) < 0) {
        logm("inotify %s: %s", ptsbuf, strerror(errno));
        return;
    }
    logm("%s → %s (UART), жду клиентов", tty_path, ptsbuf);

    int sock = -1, tn = 0, users = 0;
    unsigned baud = 0;
    time_t last_try = 0;
    while (!stop) {
        struct pollfd pf[3] = {{ino, POLLIN, 0}, {master, POLLIN, 0}, {sock, POLLIN, 0}};
        int nfd = users > 0 ? (sock >= 0 ? 3 : 2) : 1;
        int r = poll(pf, nfd, users > 0 ? 250 : -1);
        if (r < 0) { if (errno == EINTR) continue; break; }

        if (pf[0].revents & POLLIN) {
            char evb[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
            ssize_t k;
            while ((k = read(ino, evb, sizeof evb)) > 0) {
                for (char *p = evb; p < evb + k; ) {
                    struct inotify_event *e = (struct inotify_event *)p;
                    if (e->mask & IN_OPEN) users++;
                    if (e->mask & IN_CLOSE) users--;
                    p += sizeof *e + e->len;
                }
            }
            if (users < 0) users = 0;
            if (users == 0 && sock >= 0) {
                close(sock); sock = -1;
                logm("UART: клиентов нет, соединение с S9 закрыто");
            }
        }
        if (users == 0) continue;

        if (sock < 0 && time(NULL) - last_try >= 2) {
            last_try = time(NULL);
            if ((sock = tcp_connect(PORT_RFC2217)) >= 0) {
                uint8_t neg[] = {IAC, WILL, COMPORT, IAC, DO, COMPORT};
                write_all(sock, neg, sizeof neg);
                baud = 0;
                tn = 0;
                logm("UART: подключено к %s:%s", host, PORT_RFC2217);
            } else {
                logm("UART: нет связи с %s:%s", host, PORT_RFC2217);
            }
        }
        unsigned b = term_baud(master);     // у PTY настройки общие: читаем через master
        if (sock >= 0 && b && b != baud && send_baud(sock, b) == 0) { baud = b; logm("UART: %u бод", b); }

        uint8_t buf[4096], o[8192];
        if (nfd > 1 && (pf[1].revents & POLLIN)) {
            ssize_t k = read(master, buf, sizeof buf);
            if (k > 0 && sock >= 0) {
                int j = 0;
                for (ssize_t i = 0; i < k; i++) { o[j++] = buf[i]; if (buf[i] == IAC) o[j++] = IAC; }
                if (write_all(sock, o, j)) { close(sock); sock = -1; }
            }
        }
        if (nfd > 2 && sock >= 0 && (pf[2].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t k = recv(sock, buf, sizeof buf, 0);
            if (k <= 0) { logm("UART: соединение потеряно"); close(sock); sock = -1; continue; }
            int j = 0;
            for (ssize_t i = 0; i < k; i++) {
                uint8_t c = buf[i];
                switch (tn) {         // 0 данные, 1 IAC, 2 опция, 3 SB, 4 IAC в SB
                case 0: if (c == IAC) tn = 1; else o[j++] = c; break;
                case 1:
                    if (c == IAC) { o[j++] = IAC; tn = 0; }
                    else if (c == SB) tn = 3;
                    else if (c >= WILL) tn = 2;
                    else tn = 0;
                    break;
                case 2: tn = 0; break;
                case 3: if (c == IAC) tn = 4; break;
                case 4: tn = (c == SE) ? 0 : 3; break;
                }
            }
            if (j) {
                ssize_t w = write(master, o, j);
                (void)w;
            }
        }
    }
    unlink(tty_path);
}

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static void usage(void)
{
    fprintf(stderr,
        "s9bridge [адрес S9] [--config файл] [--tty путь] [--i2c N] [--spi N.M]\n"
        "         [--no-tty] [--no-i2c] [--no-spi]\n"
        "Настройки по умолчанию — из /etc/s9bridge.conf:\n"
        "  host = 192.168.10.225\n  tty  = /dev/ttyETH0\n  i2c  = 10      (off — не создавать)\n"
        "  spi  = 10.0    (off — не создавать)\n");
    exit(2);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static void set_option(const char *k, const char *v)
{
    int off = !strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "0");
    if (!strcmp(k, "host")) host = strdup(v);
    else if (!strcmp(k, "tty")) { if (off) use_tty = 0; else { tty_path = strdup(v); use_tty = 1; } }
    else if (!strcmp(k, "i2c")) { if (off && strcmp(v, "0")) use_i2c = 0; else { i2c_bus = atoi(v); use_i2c = 1; } }
    else if (!strcmp(k, "spi")) {
        if (!strcmp(v, "off") || !strcmp(v, "no")) use_spi = 0;
        else if (sscanf(v, "%d.%d", &spi_bus, &spi_cs) >= 1) use_spi = 1;
    } else logm("%s: неизвестный параметр «%s»", config_path, k);
}

static void load_config(void)
{
    FILE *f = fopen(config_path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (*k && *v) set_option(k, v);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc - 1; i++)
        if (!strcmp(argv[i], "--config")) config_path = argv[i + 1];
    load_config();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--tty") && i + 1 < argc) set_option("tty", argv[++i]);
        else if (!strcmp(argv[i], "--i2c") && i + 1 < argc) set_option("i2c", argv[++i]);
        else if (!strcmp(argv[i], "--spi") && i + 1 < argc) set_option("spi", argv[++i]);
        else if (!strcmp(argv[i], "--no-tty")) use_tty = 0;
        else if (!strcmp(argv[i], "--no-i2c")) use_i2c = 0;
        else if (!strcmp(argv[i], "--no-spi")) use_spi = 0;
        else if (argv[i][0] == '-') usage();
        else host = argv[i];
    }
    if (!host) {
        logm("не задан адрес S9: укажите его в %s (host = ...) или аргументом", config_path);
        usage();
    }
    logm("S9: %s", host);

    static struct cuse_dev di = {.ops = &i2c_ops}, ds = {.ops = &spi_ops};
    snprintf(di.name, sizeof di.name, "i2c-%d", i2c_bus);
    snprintf(ds.name, sizeof ds.name, "spidev%d.%d", spi_bus, spi_cs);
    if (use_i2c) cuse_start(&di);
    if (use_spi) cuse_start(&ds);

    // свои обработчики поверх libfuse: остановиться и убрать ссылку на PTY
    struct sigaction sa = {.sa_handler = on_signal};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (use_tty) uart_loop();
    else while (!stop) pause();

    logm("остановка");
    // устройства CUSE исчезнут при закрытии /dev/cuse вместе с процессом
    _exit(0);
}
