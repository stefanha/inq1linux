#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <pty.h>
#include <usb.h>

/* USB serial driver for INQ1 modem.  The modem is accessed using
 * bulk reads and writes.  There is also an interrupt endpoint.
 */

enum {
    VENDOR_AMOI = 0x1614,
    PRODUCT_INQ1 = 0x0408,
    INTF_SERIAL = 0, /* USB interface */
    EP_SERIAL = 2, /* USB endpoint.  Note there is also endpoint #1
                    * which provides interrupts but we ignore it. */
    TIMEOUT = 0, /* milliseconds */
};

static struct usb_dev_handle *devh;
static int fd;

#if 0
static void set_color(int c)
{
    fprintf(stderr, "\x1b[%dm", 30 + c);
}

static void print_buf(char *buf, int len)
{
    int i;
    fprintf(stderr, "\"");
    for (i = 0; i < len; i++) {
        fprintf(stderr, "%c", isprint(buf[i]) ? buf[i] : '?');
    }
    fprintf(stderr, "\"");
}

static ssize_t my_read(int fd, char *buf, size_t len)
{
    int nread;
    set_color(1);
    fprintf(stderr, "read(%d, ", fd);
    nread = read(fd, buf, len);
    set_color(1);
    print_buf(buf, nread);
    fprintf(stderr, ", %u) = %d\n", len, nread);
    return nread;
}

static ssize_t my_write(int fd, char *buf, size_t len)
{
    int nwritten;
    set_color(2);
    fprintf(stderr, "write(%d, ", fd);
    print_buf(buf, len);
    fprintf(stderr, ", %d) = ", len);
    nwritten = write(fd, buf, len);
    set_color(2);
    fprintf(stderr, "%d\n", nwritten);
    return nwritten;
}

static int my_usb_bulk_read(struct usb_dev_handle *devh, int ep, char *buf, int len, int timeout)
{
    int nread;
    set_color(3);
    fprintf(stderr, "usb_bulk_read(");
    nread = usb_bulk_read(devh, ep, buf, len, timeout);
    set_color(3);
    print_buf(buf, nread);
    fprintf(stderr, ") = %d\n", nread);
    return nread;
}

static int my_usb_bulk_write(struct usb_dev_handle *devh, int ep, char *buf, int len, int timeout)
{
    int nwritten;
    set_color(4);
    fprintf(stderr, "usb_bulk_write(");
    print_buf(buf, len);
    fprintf(stderr, ", %d) = ", len);
    nwritten = usb_bulk_write(devh, ep, buf, len, timeout);
    set_color(4);
    fprintf(stderr, "%d\n", nwritten);
    return nwritten;
}
#else
#define my_read read
#define my_write write
#define my_usb_bulk_read usb_bulk_read
#define my_usb_bulk_write usb_bulk_write
#endif

static void init(void)
{
    usb_init();
    usb_find_busses();
    usb_find_devices();
}

static struct usb_device *find_device(uint16_t vendor, uint16_t product)
{
    struct usb_bus *bus;
    for (bus = usb_busses; bus; bus = bus->next) {
        struct usb_device *dev;
        for (dev = bus->devices; dev; dev = dev->next) {
            struct usb_device_descriptor *desc = &dev->descriptor;
            if (desc->idVendor == vendor && desc->idProduct == product) {
                return dev;
            }
        }
    }
    return NULL;
}

static struct usb_device *wait_for_device(uint16_t vendor, uint16_t product)
{
    int i;
    printf("waiting for USB device %04x:%04x...\n", vendor, product);
    for (i = 0; i < 60; i++) {
        struct usb_device *dev = find_device(vendor, product);
        if (dev) {
            return dev;
        }
        sleep(1);
    }
    fprintf(stderr, "timeout\n");
    exit(1);
}

/* Read from pty, write to USB device */
static void pty_to_usb(void)
{
    char buf[BUFSIZ];
    char *p;
    ssize_t nread;
    int rc;
    while ((nread = my_read(fd, buf, sizeof buf)) > 0) {
        p = buf;
        do {
            if ((rc = my_usb_bulk_write(devh, EP_SERIAL, buf, nread, TIMEOUT)) < 0) {
                if (rc == -ETIMEDOUT) {
                    continue;
                }
                fprintf(stderr, "usb_bulk_write failed (%d)\n", rc);
                exit(1);
            }
            p += rc;
            nread -= rc;
        } while (nread > 0);
    }
    fprintf(stderr, "read failed (%d)\n", errno);
}

/* Read from USB device, write to pty */
static void *usb_to_pty(void *unused)
{
    char buf[BUFSIZ];
    int nread;
    do {
        while ((nread = my_usb_bulk_read(devh, EP_SERIAL, buf, sizeof buf, TIMEOUT)) >= 0) {
            ssize_t nwritten;
            char *p = buf;
            do {
               nwritten = my_write(fd, buf, nread);
               if (nwritten < 0) {
                   fprintf(stderr, "write failed (%d)\n", errno);
                   exit(1);
               }
               p += nwritten;
               nread -= nwritten;
            } while (nread > 0);
        }
    } while (nread == -ETIMEDOUT);
    fprintf(stderr, "usb_bulk_read failed (%d)\n", nread);
    return NULL;
}

int main(int argc, char **argv)
{
    struct usb_device *dev;
    int slave;
    char path[PATH_MAX];
    pthread_t thread;
    int rc;

    init();
    dev = wait_for_device(VENDOR_AMOI, PRODUCT_INQ1);
    devh = usb_open(dev);
    if (!devh) {
        fprintf(stderr, "usb_open failed\n");
        exit(1);
    }

    if ((rc = usb_claim_interface(devh, INTF_SERIAL)) < 0) {
        fprintf(stderr, "usb_claim_interface failed (%d)\n", rc);
        exit(1);
    }

    if (openpty(&fd, &slave, path, NULL, NULL)) {
        fprintf(stderr, "openpty failed\n");
        exit(1);
    }
    printf("%s\n", path);
    fflush(stdout);

    pthread_create(&thread, NULL, usb_to_pty, NULL);
    pty_to_usb();

    usb_release_interface(devh, INTF_SERIAL);
    usb_close(devh);
    return 0;
}
