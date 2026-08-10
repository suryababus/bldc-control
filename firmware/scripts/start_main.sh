#!/usr/bin/env bash
#
# Attach to the freshly-flashed TLE9879 after `make flash`.
#
# Default: connect and leave the core HALTED (the Makefile parks it in a RAM
# spin loop so a bad firmware can't trip the PSU on the bench).
#
# With MOTORDRIVE_RUN=1: reset, set a breakpoint at main(), and run until it
# hits — the core stops at main() entry, ready for the debugger.
#
# Usage: start_main.sh <firmware.elf> [jlink-speed]
set -euo pipefail

ELF="${1:?usage: start_main.sh <firmware.elf> [speed]}"
SPEED="${2:-400}"

JLINK="$(command -v JLinkExe || echo /usr/local/bin/JLinkExe)"
NM="$(command -v arm-none-eabi-nm || \
      echo "$HOME/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm")"

main_addr="$("$NM" "$ELF" 2>/dev/null | awk '$2 == "T" && $3 == "main" { print $1; exit }')"

cmd="si SWD
speed $SPEED
device TLE9879-2QXA40
connect
halt"

if [ -n "${MOTORDRIVE_RUN:-}" ] && [ -n "$main_addr" ]; then
  cmd="$cmd
b 0x$main_addr
r
g
sleep 300
halt"
fi

cmd="$cmd
exit"

printf '%s\n' "$cmd" > /tmp/start_main.jlink
"$JLINK" -NoGui 1 -CommanderScript /tmp/start_main.jlink

if [ -n "$main_addr" ]; then
  echo "start_main: main() at 0x$main_addr"
fi
echo "start_main: core halted (set MOTORDRIVE_RUN=1 to stop at main)"
