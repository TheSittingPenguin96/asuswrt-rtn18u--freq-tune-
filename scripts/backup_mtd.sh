#!/bin/bash
# Stream MTD partitions over SSH directly to PC files (no staging on router /tmp).
#
# Why no staging on the router? RT-N18U has ~110MB free RAM and /tmp is tmpfs,
# so writing 60+ MB partitions there can OOM the box or fail with "can't fork".
# Piping dd straight back over the SSH socket avoids that entirely.
#
# Each partition gets dumped, md5'd locally AND remotely, and the script aborts
# on any mismatch. Output goes into a fresh timestamped folder so we never
# clobber an earlier known-good backup.

set -e  # bail immediately on any failure (don't keep going with a corrupt dump)

# Timestamped destination so multiple runs don't overwrite each other.
TS=$(date +%Y%m%d_%H%M%S)
DEST=~/AsusWRT/firmware/mtd_backup/$TS
mkdir -p "$DEST"
cd "$DEST"
echo "=== Dest: $DEST ==="

# Belt-and-braces: clear any leftover staging files from previous (older) runs
# that DID stage on the router. Harmless if nothing is there.
ssh -F /tmp/ssh_router_cfg router 'rm -f /tmp/mtd*.bin /tmp/mtd.md5 2>/dev/null; echo "router /tmp cleaned"'

# Friendly names matching /proc/mtd on this firmware (RT-N18U / Merlin 386.x).
# Index in the array == the mtdN number, so mtd0=boot (CFE), mtd1=nvram, etc.
NAMES=(boot nvram linux rootfs brcmnand asus)

for i in 0 1 2 3 4 5; do
  NAME=${NAMES[$i]}
  echo "--- mtd${i} (${NAME}) ---"
  # 1) Stream raw bytes from router /dev/mtdN to local file.
  #    `2>/dev/null` on the remote side hides dd's stderr summary so it doesn't
  #    get mixed into the binary stream coming back over stdout.
  ssh -F /tmp/ssh_router_cfg router "dd if=/dev/mtd${i} 2>/dev/null" > "mtd${i}.bin"
  SIZE=$(stat -c %s "mtd${i}.bin")
  LOCAL_MD5=$(md5sum "mtd${i}.bin" | awk '{print $1}')
  # 2) Independently re-read the partition on the router and md5 it there.
  #    This is essentially free thanks to the SSH ControlMaster set up in
  #    /tmp/ssh_router_cfg (no second TCP/auth round-trip).
  #    If LOCAL != REMOTE the transfer was corrupted -> abort, don't trust it.
  REMOTE_MD5=$(ssh -F /tmp/ssh_router_cfg router "dd if=/dev/mtd${i} 2>/dev/null | md5sum" | awk '{print $1}')
  if [[ "$LOCAL_MD5" == "$REMOTE_MD5" ]]; then
    printf "  OK  size=%-9s md5=%s\n" "$SIZE" "$LOCAL_MD5"
  else
    # NOTE: mtd4 (brcmnand) is the whole-NAND view that includes the live UBI
    # volume; two back-to-back reads can legitimately differ. If you only ever
    # see mismatches on mtd4 it is expected and not a real corruption.
    echo "  MISMATCH! local=$LOCAL_MD5 remote=$REMOTE_MD5 size=$SIZE"
    exit 1
  fi
done

# Single combined manifest -- this is the file you check into git / diff
# against future backups to prove nothing has silently changed.
echo "=== Combined MD5SUMS ==="
md5sum mtd*.bin | tee MD5SUMS
echo "=== Files ==="
ls -la
echo "=== DONE: $DEST ==="
#                                .:xxxxxxxx:.
#                             .xxxxxxxxxxxxxxxx.
#                            :xxxxxxxxxxxxxxxxxxx:.
#                           .xxxxxxxxxxxxxxxxxxxxxxx:
#                          :xxxxxxxxxxxxxxxxxxxxxxxxx:
#                          xxxxxxxxxxxxxxxxxxxxxxxxxxX:
#                          xxx:::xxxxxxxx::::xxxxxxxxx:
#                         .xx:   ::xxxxx:     :xxxxxxxx
#                         :xx  x.  xxxx:  xx.  xxxxxxxx
#                         :xx xxx  xxxx: xxxx  :xxxxxxx
#                         'xx 'xx  xxxx:. xx'  xxxxxxxx
#                          xx ::::::xx:::::.   xxxxxxxx
#                          xx:::::.::::.:::::::xxxxxxxx
#                          :x'::::'::::':::::':xxxxxxxxx.
#                          :xx.::::::::::::'   xxxxxxxxxx
#                          :xx: '::::::::'     :xxxxxxxxxx.
#                         .xx     '::::'        'xxxxxxxxxx.
#                       .xxxx                     'xxxxxxxxx.
#                     .xxxx                         'xxxxxxxxx.
#                   .xxxxx:                          xxxxxxxxxx.
#                  .xxxxx:'                          xxxxxxxxxxx.
#                 .xxxxxx:::.           .       ..:::_xxxxxxxxxxx:.
#                .xxxxxxx''      ':::''            ''::xxxxxxxxxxxx.
#                xxxxxx            :                  '::xxxxxxxxxxxx
#               :xxxx:'            :                    'xxxxxxxxxxxx:
#              .xxxxx              :                     ::xxxxxxxxxxxx
#              xxxx:'                                    ::xxxxxxxxxxxx
#              xxxx               .                      ::xxxxxxxxxxxx.
#          .:xxxxxx               :                      ::xxxxxxxxxxxx::
#          xxxxxxxx               :                      ::xxxxxxxxxxxxx:
#          xxxxxxxx               :                      ::xxxxxxxxxxxxx:
#          ':xxxxxx               '                      ::xxxxxxxxxxxx:'
#            .:. xx:.                                   .:xxxxxxxxxxxxx'
#          ::::::.'xx:.            :                  .:: xxxxxxxxxxx':
#  .:::::::::::::::.'xxxx.                            ::::'xxxxxxxx':::.
#  ::::::::::::::::::.'xxxxx                          :::::.'.xx.'::::::.
#  ::::::::::::::::::::.'xxxx:.                       :::::::.'':::::::::
#  ':::::::::::::::::::::.'xx:'                     .'::::::::::::::::::::..
#    :::::::::::::::::::::.'xx                    .:: :::::::::::::::::::::::
#  .:::::::::::::::::::::::. xx               .::xxxx :::::::::::::::::::::::
#  :::::::::::::::::::::::::.'xxx..        .::xxxxxxx ::::::::::::::::::::'
#  '::::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::::'
#    '::::::::::::::::::::::: xxxxxxxxxxxxxxxxxxxxxxx :::::::::::::::'
#        ':::::::::::::::::::_xxxxxx::'''::xxxxxxxxxx '::::::::::::'
#             '':.::::::::::'                        `._'::::::'' 
# -----------------------------TheSittingPenguin96-----------------------------
