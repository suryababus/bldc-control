# motor-drive — TLE9879-2QXA40

Hobby firmware for Infineon **TLE9879-2QXA40** (MOTIX™, Cortex-M3, 3-phase motor + LIN).

## Hello world (GCC + J-Link)

```bash
brew install arm-none-eabi-gcc segger-jlink   # once
make            # build build/hello_world.{elf,hex,bin}
make flash      # Segger J-Link SWD — use device TLE9879-2QXA40 (not generic M3)
```

`src/main.c` blinks EvalKit **P0.2** (LED2 via JP7) and keeps `g_hello` for the debugger. Always service WDT1 in the main loop.

## Optional Infineon SDK path

- Keil µVision5 + **Infineon.TLE987x_DFP** + Config Wizard for MOTIX™ MCU
- Set `CMSIS_PACK_ROOT` / `KEIL_PACK_PATH` for IntelliSense/SVD

## Device constraints

- ~6 KiB RAM, ~124 KiB code flash — keep code and data small
- Bridge (BDRV) and CCU6 PWM are safety-sensitive; enable last, with dead-time and fault handling
