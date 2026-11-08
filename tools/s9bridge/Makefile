CFLAGS  ?= -O2 -g -Wall -Wextra
CFLAGS  += $(shell pkg-config --cflags fuse3)
LDLIBS  += $(shell pkg-config --libs fuse3) -lutil -lpthread
PREFIX  ?= /usr/local

s9bridge: s9bridge.c

install: s9bridge
	install -Dm755 s9bridge $(DESTDIR)$(PREFIX)/bin/s9bridge
	install -Dm644 s9bridge.service $(DESTDIR)/etc/systemd/system/s9bridge.service
	[ -f $(DESTDIR)/etc/s9bridge.conf ] || install -Dm644 s9bridge.conf $(DESTDIR)/etc/s9bridge.conf

clean:
	rm -f s9bridge

.PHONY: install clean
