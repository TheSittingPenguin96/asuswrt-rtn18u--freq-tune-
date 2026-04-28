/* mtd_raw_write: minimal raw MTD writer for NAND/NOR.
 * Erases enough blocks then page-writes the input file at offset 0.
 * Usage: mtd_raw_write <input_file> <mtd_device>
 *   e.g. mtd_raw_write /jffs/mtd0.patched.bin /dev/mtd0
 *
 * Skips bad blocks (NAND). Verifies write by read-back compare.
 *
 * Why this exists: stock ASUS `mtd-write2` refuses any image without a TRX
 * header ("Bad trx header"), which makes it useless for re-flashing CFE/NVRAM
 * partitions that are not TRX-wrapped. This tool talks straight to the MTD
 * ioctls so the input bytes hit flash unchanged.
 *
 * NOTE on this project: ultimately we did NOT use this binary on the router,
 * because building it against glibc produces a binary that the 2.6.36 kernel
 * rejects with "FATAL: kernel too old". Writing through /dev/mtdblock0 with
 * plain `dd` worked instead. Keeping this source as a fallback / reference --
 * a musl rebuild would be runnable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdint.h>

/* MTD ioctls (from <mtd/mtd-user.h>).
 * We redefine them inline so we don't need the kernel headers installed in
 * the cross-compile sysroot -- one less moving part. Values must match the
 * kernel exactly or every ioctl will EINVAL. */
#define MEMGETINFO   _IOR('M', 1, struct mtd_info_user)   /* query geometry */
#define MEMERASE     _IOW('M', 2, struct erase_info_user) /* erase one block */
#define MEMUNLOCK    _IOW('M', 6, struct erase_info_user) /* unlock for write (NOR) */
#define MEMGETBADBLOCK _IOW('M', 11, uint64_t)            /* NAND: is this block bad? */

/* Same structs the kernel uses; field order matters. */
struct mtd_info_user {
    uint8_t  type;
    uint32_t flags;
    uint32_t size;
    uint32_t erasesize;
    uint32_t writesize;
    uint32_t oobsize;
    uint64_t padding;
};
struct erase_info_user {
    uint32_t start;
    uint32_t length;
};

/* Loop until the full buffer is read or we hit EOF -- read(2) is allowed to
 * return short, especially on character devices like /dev/mtdN. */
static int read_full(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char*)buf + off, n - off);
        if (r < 0) { if (errno == EINTR) continue; return -1; } /* retry on signal */
        if (r == 0) return (int)off;                            /* short EOF */
        off += (size_t)r;
    }
    return (int)off;
}
/* Same idea for write(2): keep going until everything is flushed. */
static int write_full(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, (const char*)buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return (int)off;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input> <mtd_dev>\n", argv[0]);
        return 1;
    }
    const char *input = argv[1];
    const char *dev   = argv[2];

    /* Open the source image and find out how many bytes we have to flash. */
    int infd = open(input, O_RDONLY);
    if (infd < 0) { perror("open input"); return 2; }
    struct stat st;
    if (fstat(infd, &st) < 0) { perror("fstat"); return 2; }
    size_t sz = (size_t)st.st_size;

    /* O_SYNC so writes hit the device before write() returns -- on flash you
     * really don't want the kernel to defer the I/O across an ioctl. */
    int mtdfd = open(dev, O_RDWR | O_SYNC);
    if (mtdfd < 0) { perror("open mtd"); return 3; }

    /* Ask the kernel for partition geometry: total size, erase block size,
     * write/page size, NAND OOB size, and whether this is NAND or NOR. */
    struct mtd_info_user info;
    if (ioctl(mtdfd, MEMGETINFO, &info) < 0) { perror("MEMGETINFO"); return 4; }
    fprintf(stderr, "mtd: type=%u size=%u erasesize=%u writesize=%u oobsize=%u\n",
            info.type, info.size, info.erasesize, info.writesize, info.oobsize);

    if (sz > info.size) {
        fprintf(stderr, "input too large (%zu > %u)\n", sz, info.size);
        return 5;
    }

    /* Read entire input into RAM (small images only -- intended for the 512KB
     * CFE partition; do not point this at a 64MB rootfs without rethinking). */
    uint8_t *buf = malloc(sz);
    if (!buf) { perror("malloc"); return 6; }
    if (read_full(infd, buf, sz) != (int)sz) { fprintf(stderr, "short read\n"); return 7; }
    close(infd);

    /* MTD writes must be a multiple of writesize (page size on NAND, 1 on NOR).
     * Pad the tail with 0xff (erased-flash value) so we don't program stray
     * zero bits into the unused bytes of the last page. */
    uint32_t wsz = info.writesize ? info.writesize : 1;
    size_t padded = ((sz + wsz - 1) / wsz) * wsz;
    if (padded > sz) {
        buf = realloc(buf, padded);
        memset(buf + sz, 0xff, padded - sz);
    }

    /* Erases happen one full erase-block at a time, so figure out how many
     * blocks the (padded) image actually spans. */
    uint32_t esz = info.erasesize;
    uint32_t erase_total = ((padded + esz - 1) / esz) * esz;
    fprintf(stderr, "erase: 0..0x%x (%u bytes)\n", erase_total, erase_total);

    /* NOR parts can be software-locked; tell the kernel to drop the lock on
     * the region we're about to erase. EOPNOTSUPP just means "this part has
     * no lock concept" (typical for NAND), which is fine -- swallow it. */
    {
        struct erase_info_user u = { .start = 0, .length = erase_total };
        if (ioctl(mtdfd, MEMUNLOCK, &u) < 0 && errno != EOPNOTSUPP)
            fprintf(stderr, "MEMUNLOCK warning: %s\n", strerror(errno));
    }

    /* Walk through the region one erase-block at a time. Every block has to
     * be erased before it can be (re)written -- flash bits can only flip
     * 1->0 on a write; only an erase resets them back to 1. */
    uint32_t off = 0;
    while (off < erase_total) {
        /* On NAND, ask the controller's bad-block table whether this block
         * is marked bad. Erasing/writing a bad block can corrupt neighbours,
         * so we just skip it -- callers must keep the patched image small
         * enough that skipping a block isn't catastrophic. */
        if (info.type == 4 /* MTD_NANDFLASH */ || info.type == 8 /* MTD_MLCNANDFLASH */) {
            uint64_t boff = off;
            int bad = ioctl(mtdfd, MEMGETBADBLOCK, &boff);
            if (bad > 0) {
                fprintf(stderr, "  bad block at 0x%x \u2014 skipping\n", off);
                off += esz;
                continue;
            }
        }
        /* Actually erase this block. After this, every byte in the block
         * reads as 0xff until we write something. */
        struct erase_info_user e = { .start = off, .length = esz };
        if (ioctl(mtdfd, MEMERASE, &e) < 0) {
            fprintf(stderr, "MEMERASE @0x%x failed: %s\n", off, strerror(errno));
            return 8;
        }
        fprintf(stderr, "  erased 0x%x\n", off);
        off += esz;
    }

    /* Now program the new contents. We rewind to offset 0 because the erase
     * loop didn't move the file pointer (ioctls don't), but to be explicit
     * and safe we lseek anyway. */
    if (lseek(mtdfd, 0, SEEK_SET) < 0) { perror("lseek"); return 9; }
    if (write_full(mtdfd, buf, padded) != (int)padded) {
        fprintf(stderr, "write short/failed: %s\n", strerror(errno));
        return 10;
    }
    fprintf(stderr, "wrote %zu bytes\n", padded);

    /* Read it all back and compare byte-for-byte. This is the only way to
     * notice a flaky cell or a botched write before we reboot into it -- on
     * CFE there is no second chance, a corrupt image bricks the board. */
    if (lseek(mtdfd, 0, SEEK_SET) < 0) { perror("lseek2"); return 11; }
    uint8_t *vbuf = malloc(padded);
    if (read_full(mtdfd, vbuf, padded) != (int)padded) {
        fprintf(stderr, "verify read failed\n"); return 12;
    }
    if (memcmp(buf, vbuf, padded) != 0) {
        /* Find and report the first differing byte to make debugging easier. */
        size_t i;
        for (i = 0; i < padded; i++) if (buf[i] != vbuf[i]) break;
        fprintf(stderr, "VERIFY MISMATCH at offset 0x%zx (wrote 0x%02x, read 0x%02x)\n",
                i, buf[i], vbuf[i]);
        return 13;
    }
    fprintf(stderr, "verify OK\n");

    close(mtdfd);
    free(buf);
    free(vbuf);
    return 0;
}
/*
                                .:xxxxxxxx:.
                             .xxxxxxxxxxxxxxxx.
                            :xxxxxxxxxxxxxxxxxxx:.
                           .xxxxxxxxxxxxxxxxxxxxxxx:
                          :xxxxxxxxxxxxxxxxxxxxxxxxx:
                          xxxxxxxxxxxxxxxxxxxxxxxxxxX:
                          xxx:::xxxxxxxx::::xxxxxxxxx:
                         .xx:   ::xxxxx:     :xxxxxxxx
                         :xx  x.  xxxx:  xx.  xxxxxxxx
                         :xx xxx  xxxx: xxxx  :xxxxxxx
                         'xx 'xx  xxxx:. xx'  xxxxxxxx
                          xx ::::::xx:::::.   xxxxxxxx
                          xx:::::.::::.:::::::xxxxxxxx
                          :x'::::'::::':::::':xxxxxxxxx.
                          :xx.::::::::::::'   xxxxxxxxxx
                          :xx: '::::::::'     :xxxxxxxxxx.
                         .xx     '::::'        'xxxxxxxxxx.
                       .xxxx                     'xxxxxxxxx.
                     .xxxx                         'xxxxxxxxx.
                   .xxxxx:                          xxxxxxxxxx.
                  .xxxxx:'                          xxxxxxxxxxx.
                 .xxxxxx:::.           .       ..:::_xxxxxxxxxxx:.
                .xxxxxxx''      ':::''            ''::xxxxxxxxxxxx.
                xxxxxx            :                  '::xxxxxxxxxxxx
               :xxxx:'            :                    'xxxxxxxxxxxx:
              .xxxxx              :                     ::xxxxxxxxxxxx
              xxxx:'                                    ::xxxxxxxxxxxx
              xxxx               .                      ::xxxxxxxxxxxx.
          .:xxxxxx               :                      ::xxxxxxxxxxxx::
          xxxxxxxx               :                      ::xxxxxxxxxxxxx:
          xxxxxxxx               :                      ::xxxxxxxxxxxxx:
          ':xxxxxx               '                      ::xxxxxxxxxxxx:'
            .:. xx:.                                   .:xxxxxxxxxxxxx'
          ::::::.'xx:.            :                  .:: xxxxxxxxxxx':
  .:::::::::::::::.'xxxx.                            ::::'xxxxxxxx':::.
  ::::::::::::::::::.'xxxxx                          :::::.'.xx.'::::::.
  ::::::::::::::::::::.'xxxx:.                       :::::::.'':::::::::
  ':::::::::::::::::::::.'xx:'                     .'::::::::::::::::::::..
    :::::::::::::::::::::.'xx                    .:: :::::::::::::::::::::::
  .:::::::::::::::::::::::. xx               .::xxxx :::::::::::::::::::::::
  :::::::::::::::::::::::::.'xxx..        .::xxxxxxx ::::::::::::::::::::'
  '::::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::::'
    '::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::'
        ':::::::::::::::::::_xxxxxx::'''::xxxxxxxxxx '::::::::::::'
             '':.::::::::::'                        `._'::::::'' 
-----------------------------TheSittingPenguin96-----------------------------
*/
