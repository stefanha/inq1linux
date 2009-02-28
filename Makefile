# Variables that may need to be tweaked
PREFIX  = /usr/local/bin
INSTALL = install
export PREFIX INSTALL

.PHONY: all clean install
all:
	$(MAKE) -C src $@
clean:
	$(MAKE) -C src $@
install:
	$(MAKE) -C src $@
	$(INSTALL) pppd-scripts/* /etc/ppp/peers
