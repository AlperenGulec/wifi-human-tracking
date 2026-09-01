# csi-logger. Plain make, no autotools - the Yocto recipe calls this directly.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
BINDIR  ?= /usr/bin
DESTDIR ?=

all: csi-logger monoms

csi-logger: csi_logger.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

monoms: monoms.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: csi-logger monoms
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 csi-logger $(DESTDIR)$(BINDIR)/csi-logger
	install -m 0755 monoms $(DESTDIR)$(BINDIR)/monoms

clean:
	rm -f csi-logger monoms

.PHONY: all install clean
