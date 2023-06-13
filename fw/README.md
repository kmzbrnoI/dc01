DC-01 Firmware
==============

This repository contains firmware for main MCU STM32F103 of DC-01 module.

## Module functionality

TODO

## LEDs behavior

TODO

## Build & requirements

 * Development:
   - `arm-none-eabi-gcc`
   - `make`
     ```bash
     $ make
     ```

 * Debugging:
   - `openocd`
   - `arm-none-eabi-gdb`
     ```bash
     $ openocd
     $ arm-none-eabi-gdb build/dc01.elf
     (gdb) target extended-remote :3333
     (gdb) b main
     ```

 * Flashing:
   - `st-flash` (via STlink)
      DC-01 module contains programming connector. Use STlink to program the MCU.
     ```bash
     $ make flash_stlink
     ```

## License

This application is released under the [Apache License v2.0
](https://www.apache.org/licenses/LICENSE-2.0).
