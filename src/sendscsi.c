#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <scsi/sg.h>

/*
 * Send the magic SCSI command to expose the interesting
 * USB interfaces on the INQ1, including modem and
 * diagnostics.
 */
int main(int argc, char **argv)
{
    int fd;
    unsigned char cmd[] = {0x2b, /* SCSI Seek */
                           0x00, 0x00, 0x00,
                           0x00, 0x00};
    unsigned char sb[32];
    unsigned char dxfer[32];
    sg_io_hdr_t iohdr = {
        .interface_id = 'S',
        .dxfer_direction = SG_DXFER_NONE,
        .cmd_len = sizeof cmd,
        .mx_sb_len = 0,
        .iovec_count = 0,
        .dxfer_len = 0,
        .dxferp = dxfer,
        .cmdp = cmd,
        .sbp = sb,
        .timeout = 0,
        .flags = 0,
        .pack_id = 0,
        .usr_ptr = NULL,
    };

    if (argc != 2) {
        fprintf(stderr, "usage: %s DEVICE\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        err(1, "%s", argv[1]);
    }

    if (write(fd, &iohdr, sizeof iohdr) != sizeof iohdr) {
        err(1, "write");
    }

    /* For completeness, read back.  This will fail if the USB device
     * has already disconnected but it doesn't matter. */
    read(fd, &iohdr, sizeof iohdr);

    close(fd);
    return 0;
}
