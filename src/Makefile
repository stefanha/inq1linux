CFLAGS = -Wall
BINS   = sendscsi startmodem usbserial

.PHONY: all clean install
all: $(BINS)
clean:
	rm -f $(BINS)

install:
	$(INSTALL) $(BINS) $(PREFIX)

sendscsi: sendscsi.c

startmodem: startmodem.c
	$(CC) -o $@ $< -lusb

usbserial: usbserial.c
	$(CC) -o $@ $< -lusb -lutil -lpthread
