# RT-N18U CPU/DDR Frequency Tuning via CFE Patch
<!-- This file is just a summary and shouldn't be used as a DIY manual -->
End-to-end record of permanently overclocking an ASUS RT-N18U from
800 MHz CPU / 533 MHz DDR to **1000 MHz CPU / 800 MHz DDR** by patching
the CFE bootloader's embedded NVRAM defaults.

## 1. Hardware & firmware baseline

| Item            | Value                                                |
|-----------------|------------------------------------------------------|
| Router          | ASUS RT-N18U                                         |
| SoC             | Broadcom BCM47081A0 (ARMv7, single-core)             |
| Stock CPU clock | 800 MHz                                              |
| Stock DDR clock | 533 MHz                                              |
| RAM             | 256 MB                                               |
| NAND flash      | 128 MB                                               |
| Firmware        | ASUSWRT-Merlin 386.3_3 (gzenux/asuswrt-rtn18u)            |
| Bootloader      | Broadcom CFE in `mtd0` (512 KB)                      |

MTD layout as reported by `/proc/mtd`:

| Partition | Name     | Size       | Purpose                       |
|-----------|----------|------------|-------------------------------|
| mtd0      | boot     | 524 288    | CFE bootloader + NVRAM defaults |
| mtd1      | nvram    | 1 572 864  | Runtime NVRAM                 |
| mtd2      | linux    | 65 011 712 | Kernel + rootfs (TRX)         |
| mtd3      | rootfs   | 63 362 744 | rootfs subset of mtd2         |
| mtd4      | brcmnand | 65 798 144 | Whole-NAND view (UBI/JFFS)    |
| mtd5      | asus     | 1 310 720  | ASUS factory partition        |

## 2. The persistence problem I set out to solve

Setting the clock at runtime works:

```sh
nvram set clkfreq=1000,800
nvram commit
reboot
```

After reboot the CFE serial banner showed `clkfreq=1000,*800*` and Linux
reported BogoMIPS ≈ 1992 (vs. ≈ 1594 stock), proving the silicon happily
ran at 1 GHz. But user-space `nvram get clkfreq` always returned the
stock string `800,533` again, and any reboot path that re-evaluated
defaults reverted the setting. The override only "stuck" because CFE
read the user NVRAM blob before the rest of the system reset it.

Root cause (confirmed later): the CFE image carries its own copy of the
default NVRAM as a **`FLSH` block** at offset `0x400` in `mtd0`. On every
boot CFE merges these defaults into the live NVRAM. As long as that
block contained `clkfreq=800,533`, the stock value would always win.

The proper fix is therefore to change the default at its source — the
`FLSH` block inside CFE — so the new value becomes the platform default.

## 3. Backups — non-negotiable prerequisite

The board has **no JTAG header populated and no second-stage recovery
loader**. A bad CFE image is unrecoverable without dedicated hardware.
Two independent SSH-based MTD dumps were therefore taken before any
write, both stored under [firmware/mtd_backup/](firmware/mtd_backup/):

```
firmware/mtd_backup/
├── <timestamp1>/        # first complete dump
│   ├── MD5SUMS
│   ├── mtd0.bin … mtd5.bin
└── <timestamp2>/        # second dump, used as working copy
    ├── VERIFIED.md
    ├── mtd0.bin … mtd5.bin
    └── mtd4.bin.first       # second mtd4 read (NAND view, non-stable)
```

Verified MD5 hashes (taken from
[firmware/mtd_backup/20260428_141712/VERIFIED.md](firmware/mtd_backup/20260428_141712/VERIFIED.md)):

| Partition | Size       | MD5                              |
|-----------|------------|----------------------------------|
| mtd0 boot | 524 288    | `5cc85bdd2288a41f7f9964061c5edf6c` |
| mtd1 nvram| 1 572 864  | `84d5edc622b37e30592ab66c2bb7d2ed` |
| mtd2 linux| 65 011 712 | `8710fb079b8b8fc87d7e882756492174` |
| mtd3 rootfs| 63 362 744| `245b0e78f2a15798f351b6a99f68d440` |
| mtd5 asus | 1 310 720  | `804fa5d7b70c848c89111ed65a660cb3` |

`mtd4` is the whole-NAND view containing the live UBI volume; two
sequential reads disagree and that is expected — it is not used for
recovery.

Cross-check: every stable partition is **byte-identical** across the
two independent dumps taken ~40 min apart, confirming the backups are
trustworthy.

Backup dumps were taken with serial `dd` over SSH (router has only
~110 MB free RAM, so I avoided parallel pipes that triggered
`sh: can't fork`). The repeatable workflow is wrapped in
[backup_mtd.sh](../backup_mtd.sh):

```sh
./backup_mtd.sh
# → creates firmware/mtd_backup/<timestamp>/ with mtd0.bin … mtd5.bin
#   plus a MD5SUMS file. Each partition is dd'd straight from
#   /dev/mtdN over SSH into the local file, then md5'd both ends and
#   compared. Aborts on any mismatch.
```

Equivalent one-liner per partition:

```sh
ssh router 'dd if=/dev/mtdN bs=65536 2>/dev/null' > mtdN.bin
ssh router 'md5sum /dev/mtdN'   # verify against local md5sum
```

Re-run [backup_mtd.sh](../backup_mtd.sh) before any future CFE/NVRAM
write — it is the cheapest insurance against bricking.

## 4. Reverse-engineering the CFE NVRAM block

Annotated layout of `mtd0.bin`:

```
0x00000 - 0x003ff   ARM exception vectors, early boot stub
0x00400 - 0x00413   FLSH header (20 bytes, see below)
0x00414 - 0x00a85   NVRAM defaults: ASCII "key=value\0" entries
0x00a86 - 0x01403   zero padding inside FLSH region
0x01400 - 0x023ff   "AMZL" marker + more ARM code
0x24000 - 0x24003   little-endian compressed-size word (0x14a04)
0x24004 - 0x38a07   LZMA-ALONE stream (CFE main, 319 976 bytes uncompressed)
0x38a08 - 0x7ffff   0xff erase fill
```

### 4.1 FLSH header (offset 0x400, 20 bytes)

| Offset | Size | Field                           | Observed value |
|--------|------|---------------------------------|----------------|
| 0x00   | 4    | magic `'FLSH'`                  | `46 4c 53 48`  |
| 0x04   | 4    | length (header + body)          | `0x0000068c` (1676) |
| 0x08   | 1    | **CRC8** of `[hdr+9 .. hdr+len]`| `0xa9` (orig)  |
| 0x09   | 3    | crc_ver_init tail / version     | `00 00 00`     |
| 0x0c   | 4    | sdram refresh                   | `0x00000147`   |
| 0x10   | 4    | sdram ncdl                      | `0x00000000`   |
| 0x14   | …    | NUL-terminated `key=value` defaults | text       |

The CRC algorithm is the **Broadcom NVRAM CRC-8 (`hndcrc8`)** — reflected
form, reflected polynomial `0xab`, init `0xff`, no xor-out. It is computed
over byte 9 through the end of the declared length. I verified the formula
by recomputing on the unmodified image and matching the stored `0xa9`,
and again on the patched image (`0xe5`). The exact implementation lives in
[tools/patch_cfe_clkfreq.py](../tools/patch_cfe_clkfreq.py).

### 4.2 NVRAM defaults inventory

The defaults block contains 94 entries — board ID, MAC, calibration
constants for the BCM4360 radio, GPIO mapping, and crucially:

```
clkfreq=800,533
xtalfreq=25000
sdram_config=0x0147
```

Total used: 1 656 bytes of the 1 656-byte string region (1404-414h),
with 6 trailing NUL bytes available as slack inside the FLSH block.

### 4.3 LZMA payload

The bytes after the FLSH region are mostly ARM code, then a single
LZMA-ALONE stream at `0x24004`. I decompressed it (`format=FORMAT_ALONE`,
size 319 976 bytes) and confirmed it contains the CFE main ("clkfreq:
%s\n", "DDR Clock: %u MHz", "FLSH" string references, etc.). This
LZMA payload was **not** modified by us — only the small `FLSH`
defaults block was touched.

## 5. Building the patched CFE

The change required is one ASCII string:

```
clkfreq=800,533    →    clkfreq=1000,800
```

That is a +1-byte growth (16 → 17 bytes incl. NUL terminator).
Strategy:

1. In-place replace `clkfreq=800,533\0` with `clkfreq=1000,800\0`.
2. Shift all subsequent strings up by 1 byte (absorb into trailing NUL
   pad — there were 6 free bytes, I used 1).
3. Keep the FLSH header `length` field unchanged at `0x68c`.
4. Recompute CRC-8 and store it at offset `0x408`.
5. Leave everything outside `[0x400 .. 0x400+0x68c]` byte-identical:
   ARM code, the LZMA payload, and the trailing 0xff fill all
   untouched. This is critical — it means the executable code path of
   the bootloader is unchanged.

Result:

| Property        | Original                            | Patched                               |
|-----------------|--------------------------------------|---------------------------------------|
| MD5             | `5cc85bdd2288a41f7f9964061c5edf6c`   | `be3c57a216aacb94fd9b69e2c2e2e373`    |
| FLSH length     | `0x68c` (unchanged)                  | `0x68c`                               |
| FLSH CRC-8      | `0xa9`                               | `0xe5` (recomputed)                   |
| Bytes differing | —                                    | 1 483 (all inside the FLSH region)    |

The patched image is checked into
[firmware/mtd_backup/<timestampt>/mtd0.patched.bin](firmware/mtd_backup/<timestamp>/mtd0.patched.bin).

## 6. Flashing 📸 — the gotchas

The factory `mtd-write2` is hard-coded to require a TRX header (`HDR0`
magic) and refuses raw images:

```
$ mtd-write2 /jffs/mtd0.patched.bin boot
/jffs/mtd0.patched.bin: Bad trx header
```

A custom static ARM flasher
([tools/mtd_raw_write.c](tools/mtd_raw_write.c)) was written to call
`MEMUNLOCK` / `MEMERASE` / write / verify directly via MTD ioctls. It
compiles cleanly with `gcc-arm-linux-gnueabi`, but the resulting glibc
binary refused to run on the router's 2.6.36 kernel (`FATAL: kernel
too old`). A musl-based rebuild would have worked, but a simpler path
existed.

The kernel exposes `/dev/mtdblock0`, which transparently performs
read-modify-erase-write at the block layer. A round-trip test
(read-back same bytes, write, read again, MD5 unchanged) proved that
this device path is fully writable on this kernel:

```sh
dd if=/dev/mtdblock0 of=/tmp/test.bin bs=4096 count=32
dd if=/tmp/test.bin of=/dev/mtdblock0 bs=4096 conv=notrunc
# md5 unchanged
```

Final flash command:

```sh
ssh router '
  dd if=/jffs/mtd0.patched.bin of=/dev/mtdblock0 bs=4096 conv=notrunc
  sync
  md5sum /dev/mtd0
'
# → be3c57a216aacb94fd9b69e2c2e2e373  /dev/mtd0
```

The post-flash MD5 matched the patched-image MD5 byte-for-byte.

## 7. Boot 🥾
<!-- What you should write once you've cd'd into the dir; sudo screen -L -Logfile "ASUS_serial_$(date +%Y%m%d_%H%M%S).log"   /dev/ttyUSB0 115200 -->
Captured with `screen -L /dev/ttyUSB0 115200`. Consecutive cold
boots after the flash both show:

```
Info: DDR frequency set from clkfreq=1000,*800*
CPU type 0x0: 1000MHz
DDR Clock: 800 MHz
```

And from the running OS:

```
admin@RT-N18U-F4DC:/tmp/home/root# nvram get clkfreq
1000,800
```

Before the patch, `nvram get clkfreq` always returned `800,533` post-boot
even when the running CPU had been overclocked via `nvram set` in a
prior session — i.e. the override was forgotten across resets. After the
patch the value is reported authoritatively by CFE itself and survives
power-cycles.

## 8. Reproduction recipe 🍲 (condensed)

```sh
# 0. Pre-req: SSH access to router, working serial console, full mtd backup.

# 1. Pull the original CFE.
ssh router 'dd if=/dev/mtd0 bs=65536' > mtd0.bin

# 2. Patch (Python). Reads mtd0.bin, writes mtd0.patched.bin.
python3 tools/patch_cfe_clkfreq.py mtd0.bin mtd0.patched.bin
# Or with custom values:
#   python3 tools/patch_cfe_clkfreq.py mtd0.bin mtd0.patched.bin --new 1200,800

# 3. Stage on router and verify md5.
scp mtd0.patched.bin router:/jffs/
ssh router 'md5sum /jffs/mtd0.patched.bin'   # compare to local

# 4. Flash via mtdblock (kernel handles erase).
ssh router '
  md5sum /dev/mtd0
  dd if=/jffs/mtd0.patched.bin of=/dev/mtdblock0 bs=4096 conv=notrunc
  sync
  md5sum /dev/mtd0
'

# 5. Reboot, watch serial for "clkfreq=1000,*800*" and "CPU type 0x0: 1000MHz".
ssh router 'reboot'
```

## 9. Risk assessment & rollback

- The only bytes changed lie inside the `FLSH` defaults block; no ARM
  code or the LZMA-compressed CFE main was modified. CRC-8 is correct
  per the same algorithm CFE itself uses.
- `boot_wait=on` is set, so TFTP recovery for **firmware** (mtd2) is
  available — but this does **not** cover CFE itself. CFE recovery
  would require working JTAG.
- Rollback: write the verified
  [firmware/mtd_backup/<timestampt>/mtd0.bin](firmware/mtd_backup/<timestamp>/mtd0.bin)
  back to `/dev/mtdblock0` exactly as the patched image was written.
<!--
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
-->
