#include <stdio.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>
#include <pty.h>
#include <usb.h>

/* USB serial driver for INQ1 serial devices.  This includes the modem,
 * diagnostics, and NMEA interfaces.
 *
 * The modem is accessed using bulk reads and writes.  There is also an
 * interrupt endpoint.
 */

enum {
    VENDOR_AMOI = 0x1614,
    PRODUCT_INQ1 = 0x0408,
    TIMEOUT = 0, /* milliseconds */
};

/* Known interfaces */
static const struct iface_info {
    const char *name;
    int usb_iface; /* interface */
    int usb_ep; /* endpoint */
} ifaces[] = {
    {"modem", 0, 0x2},
    {"diag", 1, 0xd},
    {"nmea", 2, 0x6},
    {"pcsync", 3, 0xf},
    {NULL, 0, 0}
};

static const struct iface_info *iface;
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

static void usage(void)
{
    fprintf(stderr, "usage: usbserial [IFNAME]\n");
    exit(1);
}

static void select_iface(const char *name)
{
    for (iface = &ifaces[0]; iface->name; iface++) {
        if (!strcmp(name, iface->name)) {
            return;
        }
    }
    fprintf(stderr, "interface name must be one of: ");
    for (iface = &ifaces[0]; iface->name; iface++) {
        fprintf(stderr, "%s ", iface->name);
    }
    fprintf(stderr, "\n");
    exit(1);
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

static void init_usb(const char *name)
{
    struct usb_device *dev;
    int rc;

    select_iface(name);

    usb_init();
    usb_find_busses();
    usb_find_devices();

    dev = wait_for_device(VENDOR_AMOI, PRODUCT_INQ1);
    devh = usb_open(dev);
    if (!devh) {
        fprintf(stderr, "usb_open failed\n");
        exit(1);
    }

    if ((rc = usb_claim_interface(devh, iface->usb_iface)) < 0) {
        fprintf(stderr, "usb_claim_interface failed (%d)\n", rc);
        exit(1);
    }
}

static void init_pty(void)
{
    char path[PATH_MAX];
    struct termios tios;
    int slave;

    if (openpty(&fd, &slave, path, NULL, NULL)) {
        fprintf(stderr, "openpty failed\n");
        exit(1);
    }

    /* Ensure the pty does no special character processing */
    cfmakeraw(&tios);
    tcsetattr(fd, TCSANOW, &tios);

    /* Print path of the pty device */
    printf("%s\n", path);
    fflush(stdout);
}

/* Read from pty, write to USB device */
static void pty_to_usb(void)
{
    char buf[BUFSIZ];
    ssize_t nread;
    while ((nread = my_read(fd, buf, sizeof buf)) > 0) {
        char *p = buf;
        while (nread > 0) {
            int nwritten = my_usb_bulk_write(devh, iface->usb_ep, p, nread, TIMEOUT);
            if (nwritten <= 0) {
                fprintf(stderr, "usb_bulk_write failed (%d)\n", nwritten);
                exit(1);
            }
            p += nwritten;
            nread -= nwritten;
        }
    }
    fprintf(stderr, "read failed (%d)\n", errno);
}

/* Read from USB device, write to pty */
static void *usb_to_pty(void *unused)
{
    char buf[BUFSIZ];
    int nread;
    while ((nread = my_usb_bulk_read(devh, iface->usb_ep, buf, sizeof buf, TIMEOUT)) > 0) {
        char *p = buf;
        while (nread > 0) {
            ssize_t nwritten = my_write(fd, p, nread);
            if (nwritten <= 0) {
                fprintf(stderr, "write failed (%d)\n", errno);
                exit(1);
            }
            p += nwritten;
            nread -= nwritten;
        }
    }
    fprintf(stderr, "usb_bulk_read failed (%d)\n", nread);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t thread;

    if (argc != 2) {
        usage();
    }

    init_usb(argv[1]);
    init_pty();

    /* There are two threads, the usb -> pty transfer thread
     * and the pty -> usb transfer thread (the main thread). */
    pthread_create(&thread, NULL, usb_to_pty, NULL);
    pty_to_usb();

    usb_release_interface(devh, iface->usb_iface);
    usb_close(devh);
    return 0;
}
