# AsusWRT — personal RT-N18U tinkering notes

This is just a personal hobby project. I forked the upstream AsusWRT-Merlin
work because I wanted to permanently overclock my old **ASUS RT-N18U** from
800 MHz / 533 MHz (CPU/DDR) to **1000 MHz / 800 MHz** by patching the CFE
bootloader's embedded NVRAM defaults — and I figured I'd publish whatever
came out of that in case it helps someone hitting the same problem.

## What's actually in here

- [docs/FREQ_TUNING.md](docs/FREQ_TUNING.md) — the real write-up: backups,
  CFE FLSH block reverse-engineering, the CRC-8 algorithm, how the patch
  was built, how it was flashed, and the boot evidence that it stuck.
- [scripts/backup_mtd.sh](scripts/backup_mtd.sh) — pulls all six MTD partitions over SSH
  and verifies them with double-sided MD5. Run this **before** you touch
  anything on flash.
- [tools/mtd_raw_write.c](tools/mtd_raw_write.c) — a small raw MTD writer
  for the case where stock `mtd-write2` refuses your image. (I ended up
  not using it on the router because of a glibc / kernel-2.6.36 mismatch,
  but it's here as a reference / fallback.)

## Status

- Works on **my** router. One specific RT-N18U, one specific Merlin build,
  one specific board revision.
- I have **no intention of maintaining this**. No issues, no PRs, no
  support — I'm not going to look. If something here breaks your
  hardware, that's on you.
- I won't be tracking upstream changes. If a future Merlin build moves
  the FLSH offsets or changes the CRC, this approach will need to be
  re-derived from scratch.

## If you want to try it on your own router

Read [docs/FREQ_TUNING.md](docs/FREQ_TUNING.md) end-to-end first, in
particular the "Risk assessment & rollback" section. The short version:

- A bad CFE write **bricks the board** unless you have working JTAG.
  Mine doesn't, so I had exactly one shot.
- Always take fresh, verified MTD backups before flashing
  ([backup_mtd.sh](backup_mtd.sh)).
- The CFE FLSH offsets, CRC-8 location, and even the existence of an
  embedded defaults block are board/firmware specific. Do not blindly
  copy the patched image from this repo onto a different unit.

## Credit

All the heavy lifting — the firmware itself, the toolchain, the years of
RE — belongs to the upstream **AsusWRT-Merlin** project and the original
**Broadcom CFE** / OpenWrt communities. This repo is a thin layer of
"here's what I did to my own router on a weekend" on top of their work.

## License

Personal/hobby project published as-is. No warranty, express or implied.
"Works on my machine" applies in the strongest possible sense.
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