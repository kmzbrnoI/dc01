DCC cutter DC-01
================

DC-01 is a DCC-cutting device. It can dis/connect low-power DCC signal. DC-01
is connected to a PC via USB. Its purpose is to serve as a *safety device* – PC
application monitors railroad control software running on the PC. When software
malfunctions (no data are received from the SW for some time), DC-01 cuts DCC
so all the trains operated by the malfunctioning SW are immediately stopped.

This project contains both PCB manufacturing data and firmware for PCB's MCU.

## Features

* DC-01 connects to PC via CDC USB-C. Communication goes over standard serial
  port. It is possible to automatically detect which serial interface is DC-01 in
  Linux & Windows, see software in `sw` subfolder.
* DC-01 contains 2 2-pole relays. DCC is connected in *active* relays state
  (disconnected when powered off). Each relay disconnects both DCC wires, relays
  are serially coupled. In case of malfunction, disconnection of just one of 4
  contacts is enough for DCC cut.
* DC-01 contains self-testing equipment. Selftests are programmed in the
  firmware of DC-01, their result is reported to PC.
* Relays are controlled via monostable multivibrators. MCU on DC-01 must
  generate dynamic signal to keep relays on. When processor freezes or
  malfunctions, relays are safely disconnected.
* There are PTCs on DCC wires.
* STM32F103 MCU, USB-C connector, RJ-12 as well as screw terminal blocks for
  DCC connection, galvanically isolated output report signals, report LEDs,
  manual on/off buttons, ESD protection on USB, powered only from USB.

## PCB

See [README in `pcb` subfolder](pcb/README.md).

## Firmware

See [README in `fw` subfolder](fw/README.md).

## Software

[PC console application in Python](sw) is provided. This application connects to
DC-01 & hJOP, it monitors hJOP. When hJOP dies or requests DCC disconnect
DCC is disconnected via DC-01.

## External acoustic warning

* Box Z23 (gme.cz)
* 5.5/2.1mm connector for power supply (12V)
* 5.5/2.5mm connector for DC-01 connection

## Further reading

Functionality of DC-01 is described in [this article](https://www.kmz-brno.cz/dc01/)
(czech language only).

## Authors

DC-01 was created by a member of [Model Railroaders Club
Brno](https://www.kmz-brno.cz/), [Jan Horáček](mailto:jan.horacek@kmz-brno.cz).

## Licence

See README in subfolders for licenses regarding pcb design and software.
