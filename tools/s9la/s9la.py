#!/usr/bin/env python3
"""Клиент логического анализатора S9 (протокол sw/la/PROTOCOL.md) для проверки без sigrok.

    s9la.py <host> id
    s9la.py <host> input
    s9la.py <host> selftest [samples] [unit] [rate]    # шаблон-счётчик, проверка каждой выборки
    s9la.py <host> capture <samples> <rate> [unit] [file.bin]
"""
import socket
import struct
import sys
import time

PORT = 5555


class S9LA:
    def __init__(self, host, port=PORT):
        self.s = socket.create_connection((host, port), timeout=10)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)

    def recv_exact(self, n):
        buf = bytearray(n)
        view = memoryview(buf)
        got = 0
        while got < n:
            k = self.s.recv_into(view[got:], n - got)
            if not k:
                raise ConnectionError("соединение закрыто")
            got += k
        return bytes(buf)

    def frame(self):
        hdr = self.recv_exact(8)
        if hdr[:2] != b"S9":
            raise ValueError(f"плохой заголовок {hdr!r}")
        typ = chr(hdr[2])
        (n,) = struct.unpack("<I", hdr[4:])
        return typ, self.recv_exact(n)

    def cmd(self, line):
        self.s.sendall(line.encode() + b"\n")
        while True:
            typ, data = self.frame()
            if typ == "R":
                return data.decode()
            print(f"  (пропущен кадр {typ}, {len(data)} байт)")

    def capture(self, **conf):
        args = " ".join(f"{k}={v}" for k, v in conf.items())
        r = self.cmd(f"CONF {args}")
        print("CONF:", r)
        if not r.startswith("OK"):
            raise RuntimeError(r)
        r = self.cmd("START")
        if r != "OK":
            raise RuntimeError(r)
        t0 = time.time()
        typ, data = self.frame()
        assert typ == "H", typ
        hdr = dict(kv.split("=") for kv in data.decode().split())
        t1 = time.time()
        chunks = []
        while True:
            typ, data = self.frame()
            if typ == "D":
                chunks.append(data)
            elif typ == "E":
                end = data.decode()
                break
        t2 = time.time()
        raw = b"".join(chunks)
        return hdr, raw, end, t1 - t0, t2 - t1


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    host, what = sys.argv[1], sys.argv[2]
    la = S9LA(host)

    if what == "id":
        print(la.cmd("ID"))
    elif what == "input":
        print(la.cmd("INPUT"))
    elif what == "selftest":
        samples = int(float(sys.argv[3])) if len(sys.argv) > 3 else 1_000_000
        unit = int(sys.argv[4]) if len(sys.argv) > 4 else 4
        rate = int(float(sys.argv[5])) if len(sys.argv) > 5 else 100_000_000
        hdr, raw, end, tcap, tsend = la.capture(rate=rate, unit=unit, samples=samples, pre=0,
                                                tmask=0, tval=0, trise=0, tfall=0, pattern=1)
        print("H:", hdr, " E:", end)
        n = len(raw) // unit
        fmt = {1: "B", 2: "H", 4: "I"}[unit]
        import array
        a = array.array(fmt, raw)
        mask = (1 << (8 * unit)) - 1
        first = a[0]
        bad = sum(1 for i, v in enumerate(a) if v != (first + i) & mask)
        print(f"выборок {n}, первая {first}, ошибок {bad}")
        print(f"захват {tcap * 1000:.0f} мс, выгрузка {len(raw) / 1e6:.1f} МБ за {tsend:.2f} с "
              f"= {len(raw) / tsend / 1e6:.1f} МБ/с")
        sys.exit(0 if bad == 0 and end.startswith("OK") else 1)
    elif what == "capture":
        samples = int(float(sys.argv[3]))
        rate = int(float(sys.argv[4]))
        unit = int(sys.argv[5]) if len(sys.argv) > 5 else 4
        out = sys.argv[6] if len(sys.argv) > 6 else None
        hdr, raw, end, tcap, tsend = la.capture(rate=rate, unit=unit, samples=samples, pre=0,
                                                tmask=0, tval=0, trise=0, tfall=0, pattern=0)
        print("H:", hdr, " E:", end, f" {len(raw)} байт за {tsend:.2f} с")
        if out:
            open(out, "wb").write(raw)
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
