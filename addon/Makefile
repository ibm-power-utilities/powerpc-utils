## Makefile for powerpc-utils-sles10-addons

PPU_ADDONS_NAME = powerpc-utils-sles10-addons
VERSION = 1.0.2

SCRIPTS = ../scripts/lsdevinfo ../scripts/ls-vdev ../scripts/ls-veth ../scripts/ls-vscsi
UTILS = ../src/drmgr/drmgr ../src/drmgr/lsslot
DOCS = ../COPYRIGHT README

INSTALL = /usr/bin/install
SBIN_DIR = /usr/sbin
DOC_DIR = /usr/share/doc/packages/$(PPU_ADDONS_NAME)

PPU_TARNAME = powerpc-utils-1.2.8
PPU_ADDONS_TARNAME = $(PPU_ADDONS_NAME)-$(VERSION)

all:

install:
	$(INSTALL) -d -m 755 $(DESTDIR)$(SBIN_DIR)
	$(foreach f,$(SCRIPTS),$(INSTALL) -m 744 $f $(DESTDIR)$(SBIN_DIR);)
	$(foreach f,$(UTILS),$(INSTALL) -m 744 $f $(DESTDIR)$(SBIN_DIR);)
	$(INSTALL) -d -m 755 $(DESTDIR)$(DOC_DIR)
	$(foreach f,$(DOCS),$(INSTALL) -m 744 $f $(DESTDIR)$(DOC_DIR);)

dist:
	$(MAKE) -C .. dist
	tar -xzf ../$(PPU_TARNAME).tar.gz
	mv $(PPU_TARNAME) $(PPU_ADDONS_TARNAME)
	mkdir $(PPU_ADDONS_TARNAME)/addon
	cp Makefile $(PPU_ADDONS_TARNAME)/addon
	cp README $(PPU_ADDONS_TARNAME)/addon
	cp powerpc-utils-sles10-addons.spec $(PPU_ADDONS_TARNAME)/addon
	tar -chof $(PPU_ADDONS_TARNAME).tar $(PPU_ADDONS_TARNAME)
	gzip $(PPU_ADDONS_TARNAME).tar
	rm -rf $(PPU_ADDONS_TARNAME)
