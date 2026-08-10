# motor-drive — TLE9879-2QXA40

Hobby firmware for Infineon **TLE9879-2QXA40** (MOTIX™, Cortex-M3, 3-phase motor + LIN).

## Build (PlatformIO + J-Link)

```bash
make            # pio run -e hall   (hello-world blink — default ENV=hall)
make ENV=spin   # pio run -e spin   (legacy open-loop six-step drive)
make flash      # build + Segger J-Link SWD — device TLE9879-2QXA40 (not generic M3)
make size       # flash/RAM usage for the current ENV
```

Sources live in `firmware/src/`, linker script in `firmware/ld/`, and PlatformIO
envs are defined in `firmware/platformio.ini`. The build uses the local custom
platform `tle987x` (`~/.platformio/platforms/tle987x`, bare GCC + J-Link, no
framework). `hall`/`hold` envs are placeholders that build the safe blink until
their phase sources land; only `spin` builds the motor code.

`firmware/src/main.c` blinks EvalKit **P0.2** (LED2 via JP7) and keeps `g_hello` for the debugger. Always service WDT1 in the main loop.

## Optional Infineon SDK path

- Keil µVision5 + **Infineon.TLE987x_DFP** + Config Wizard for MOTIX™ MCU
- Set `CMSIS_PACK_ROOT` / `KEIL_PACK_PATH` for IntelliSense/SVD

## Device constraints

- ~6 KiB RAM, ~124 KiB code flash — keep code and data small
- Bridge (BDRV) and CCU6 PWM are safety-sensitive; enable last, with dead-time and fault handling
