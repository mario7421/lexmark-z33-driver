# Lexmark Z23/Z33 CUPS driver (rastertolxz33)
#
# Builds and installs the CUPS raster filter for the Lexmark Z33
# (and Z23) GDI inkjet printers. Pure userspace, no kernel parts.

CC       ?= gcc
CFLAGS   ?= -Wall -O2 -g
# Always-on hardening (kept separate so users can still override CFLAGS):
#  - _FORTIFY_SOURCE + stack-protector: catch buffer abuses at runtime
#  - PIE + full RELRO: standard exploit mitigations
HARDENING  = -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fPIE -Wformat=2
LDFLAGS   += -fPIE -Wl,-z,relro,-z,now
LDLIBS    = -lcupsimage -lcups

DRIVER_VERSION = 1.1.0

CUPS_SERVERBIN   := $(shell cups-config --serverbin)
CUPS_DATADIR     := $(shell cups-config --datadir)
CUPS_SERVERROOT  := $(shell cups-config --serverroot)
DESTDIR ?=
PREAMBLE_PATH     = $(CUPS_DATADIR)/data/z33-preamble.bin

FILTER   = src/rastertolxz33
TESTTOOL = tools/mkraster

.PHONY: all install uninstall test clean

all: $(FILTER)

$(FILTER): src/rastertolxz33.c
	$(CC) $(CFLAGS) $(HARDENING) -DDRIVER_VERSION=\"$(DRIVER_VERSION)\" \
	      -DZ33_PREAMBLE_FILENAME=\"$(PREAMBLE_PATH)\" \
	      -o $@ $< $(LDLIBS) $(LDFLAGS)

$(TESTTOOL): tools/mkraster.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: all
	install -d $(DESTDIR)$(CUPS_SERVERBIN)/filter
	install -d $(DESTDIR)$(CUPS_DATADIR)/model/Lexmark
	install -d $(DESTDIR)$(CUPS_SERVERROOT)
	install -m 0755 $(FILTER)   $(DESTDIR)$(CUPS_SERVERBIN)/filter/rastertolxz33
	install -m 0644 src/z33-preamble.bin $(DESTDIR)$(PREAMBLE_PATH)
	install -m 0644 src/lxz33.ppd  $(DESTDIR)$(CUPS_DATADIR)/model/Lexmark/
	install -m 0644 src/lxz33.convs $(DESTDIR)$(CUPS_SERVERROOT)/
	install -m 0644 src/lxz33.types  $(DESTDIR)$(CUPS_SERVERROOT)/

uninstall:
	rm -f $(DESTDIR)$(CUPS_SERVERBIN)/filter/rastertolxz33
	rm -f $(DESTDIR)$(PREAMBLE_PATH)
	rm -f $(DESTDIR)$(CUPS_DATADIR)/model/Lexmark/lxz33.ppd
	rm -f $(DESTDIR)$(CUPS_SERVERROOT)/lxz33.convs
	rm -f $(DESTDIR)$(CUPS_SERVERROOT)/lxz33.types

# Offline smoke test: render a synthetic page to CUPS raster, run it
# through the filter, and write the raw printer stream to testpage.z33.
test: $(FILTER) $(TESTTOOL)
	$(TESTTOOL) | $(FILTER) smoke 1 user testpattern 1 "" > testpage.z33
	@bytes=$$(wc -c < testpage.z33); \
	echo "OK: wrote $$bytes bytes of Z33 printer data to testpage.z33"; \
	head -c 4 testpage.z33 | od -An -tx1 | grep -q '1b 2f 81 00' \
	    && echo "OK: output starts with the Z33 preamble (1b 2f 81 00)" \
	    || { echo "FAIL: preamble missing"; exit 1; }

clean:
	rm -f $(FILTER) $(TESTTOOL) testpage.z33
