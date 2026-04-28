# Contributing

**Please do not contribute to this fork.**

This is a personal hobby project created to document one specific overclock
experiment on one specific router. It is not maintained, not actively
developed, and not a general-purpose AsusWRT variant.

## Where to contribute

If you have improvements, bugfixes, or new features for AsusWRT on the
RT-N18U (or other Broadcom routers), please contribute **upstream** instead:

- **[gzenux/asuswrt-rtn18u](https://github.com/gzenux/asuswrt-rtn18u/)** —
  the main fork this repo was derived from.
- **[AsusWRT-Merlin](https://github.com/RMerl/asuswrt-merlin.ng)** — the
  upstream Merlin project, which is much more widely used and actively
  maintained.

Those projects have proper CI/CD, a community of maintainers, and a track
record of shipping builds to thousands of routers. Your contribution will
have real impact there.

## This repo

This repo documents a one-off CFE NVRAM patch experiment, nothing more.
The code is "works on my machine" — it is not designed for reuse, not
tested across hardware variants, and not something I am going to debug or
maintain.

If you found this because you're facing the same overclock issue on your
RT-N18U, read [docs/FREQ_TUNING.md](docs/FREQ_TUNING.md) to understand
the approach, then file an issue or PR with the upstream forks listed
above — they are the right place for this work.

Thanks for understanding.
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