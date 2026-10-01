CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin
SYSTEMD ?= /usr/lib/systemd/system
DESTDIR ?=

all: lampctl

lampctl: src/lampctl.c
	$(CC) $(CFLAGS) -o $@ $<

test: lampctl
	sh tests/run.sh ./lampctl

lampctl.service: systemd/lampctl.service.in
	sed 's|@BINDIR@|$(BINDIR)|g' $< > $@

install: lampctl lampctl.service
	install -Dm755 lampctl $(DESTDIR)$(BINDIR)/lampctl
	install -Dm644 lampctl.service $(DESTDIR)$(SYSTEMD)/lampctl.service
	@[ -e $(DESTDIR)/etc/lampctl.conf ] || install -Dm644 systemd/lampctl.conf $(DESTDIR)/etc/lampctl.conf
	@echo "udev rule is NOT installed automatically: see udev/99-lampctl.rules.example"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/lampctl $(DESTDIR)$(SYSTEMD)/lampctl.service

clean:
	rm -f lampctl lampctl.service

.PHONY: all test install uninstall clean
