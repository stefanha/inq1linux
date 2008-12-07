CFLAGS=-Wall

all: sendscsi startmodem usbserial
clean:
	rm -f sendscsi startmodem usbserial

sendscsi: sendscsi.c

startmodem: startmodem.c
	$(CC) -o $@ $< -lusb

usbserial: usbserial.c
	$(CC) -o $@ $< -lusb -lutil -lpthread
