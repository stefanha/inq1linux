#include <stdio.h>
#include <stdint.h>
#include <usb.h>

/* Start the modem.  Before talking to the modem, these
 * commands are sent to a diagnostics device.
 */

enum {
    VENDOR_AMOI = 0x1614,
    PRODUCT_INQ1 = 0x0408,
    INTF_DIAG = 2, /* USB interface */
    EP_DIAG = 6, /* USB endpoint */
};

/* The exact meaning of these commands is unknown but we
 * can guess from their names.  They appear to be
 * zero-padded. */
static char CANSTART[] = "CANSTART\0\0\0";
static char MODEMBEGIN[] = "MODEMBEGIN\0";
static char APN[] = "APN:three.co.uk\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

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

static void write_cmd(struct usb_dev_handle *devh, char *data, int size)
{
    char buf[32];
    int nread;
    if (usb_bulk_write(devh, EP_DIAG, data, size, 1000) != size) {
        fprintf(stderr, "usb_bulk_write failed\n");
        exit(1);
    }
    while ((nread = usb_bulk_read(devh, EP_DIAG, buf, sizeof buf, 1000)) > 0) {
        int i;
        printf("nread = %d ", nread);
        for (i = 0; i < nread; i++) {
            printf("%c", buf[i]);
        }
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    struct usb_device *dev;
    struct usb_dev_handle *diag;
    int rc;

    init();
    dev = wait_for_device(VENDOR_AMOI, PRODUCT_INQ1);
    diag = usb_open(dev);
    if (!diag) {
        fprintf(stderr, "usb_open failed\n");
        exit(1);
    }

    if ((rc = usb_claim_interface(diag, INTF_DIAG)) < 0) {
        fprintf(stderr, "usb_claim_interface failed (%d)\n", rc);
        exit(1);
    }

    /* Run through the modem start sequence */
    write_cmd(diag, CANSTART, sizeof CANSTART);
    write_cmd(diag, MODEMBEGIN, sizeof MODEMBEGIN);
    write_cmd(diag, APN, sizeof APN);

    usb_release_interface(diag, INTF_DIAG);
    usb_close(diag);
    return 0;
}
