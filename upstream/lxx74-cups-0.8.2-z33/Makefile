#!make
DESTDIR=
INSTALL_PREAMBLE_TO=/usr/share/cups/data/z33-preamble.bin
CUPS_DATADIR=`cups-config --datadir`
CUPS_SERVERBIN=`cups-config --serverbin`
CUPS_SERVERROOT=`cups-config --serverroot`

all: rastertolxz33 lxz33.ppd

rastertolxz33: rastertolxz33.o
	gcc -o rastertolxz33 rastertolxz33.o -lcupsimage -lcups

rastertolxz33.o: rastertolxz33.c
	gcc -Wall -Werror -O2 -g -DZ33_PREAMBLE_FILENAME=\"$(INSTALL_PREAMBLE_TO)\" -c rastertolxz33.c

lxz33.ppd: lxz33.drv
	/tmp/cupsddk/bin/ppdc lxz33.drv
	mv -f ppd/lxz33.ppd .

clean:
	rm -f rastertolxz33 *.o *~ *.bak

install: rastertolxz33 lxz33.ppd
	if [ -z "$(CUPS_SERVERBIN)" ]; then exit 1; fi
	mkdir -p $(DESTDIR)$(CUPS_SERVERBIN)/filter
	mkdir -p $(DESTDIR)$(CUPS_DATADIR)/model
	mkdir -p $(DESTDIR)$(CUPS_SERVERROOT)
	install -m 0644 -D z33-preamble.bin $(DESTDIR)$(INSTALL_PREAMBLE_TO)
	install -m 0755 rastertolxz33 $(DESTDIR)$(CUPS_SERVERBIN)/filter
	install -m 0644 lxz33.ppd $(DESTDIR)$(CUPS_DATADIR)/model
	install -m 0644 lxz33.convs $(DESTDIR)$(CUPS_SERVERROOT)
	install -m 0644 lxz33.types $(DESTDIR)$(CUPS_SERVERROOT)
