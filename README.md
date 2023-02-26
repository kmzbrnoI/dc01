DCC cutter DC-01
================

DC-01 is a DCC-cutting device. It can dis/connect low-power DCC cable. DC-01 is
connected to a PC via USB. Its purpose is to serve as a safety device – DC-01
monitors railroad control software running on the PC. When software
malfunctions (no data are received from the SW for some time), DC-01 cuts DCC
so all the trains operated by the malfunctioning SW are immediately stopped.

This project contains both PCB manufacturing data and firmware for PCB's MCU.

## Design

Schematics & PCB are designed in KiCad 6.

## Production

PCB is prepared to be automatically assembled in [JLCPCB](https://jlcpcb.com/).
SMD parts on **top** side should be assembled. Each SMD part has its `LCSC_ITEM`
attribute set.

```bash
$ make fab
```

## Authors

DC-01 is designed by members of [Model Railroaders Club
Brno](https://www.kmz-brno.cz/), [Jan Horáček](mailto:jan.horacek@kmz-brno.cz).

## License

Content of the repository is provided under [Creative Commons
Attribution-ShareAlike 4.0
License](https://creativecommons.org/licenses/by-sa/4.0/) as openhardware
project. You may download any data, contribute to the project, create PCB
yourself or even sell it yourself.
