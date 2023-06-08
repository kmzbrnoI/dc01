DC-01 ↔ PC communication protocol
=================================

Communication with PC takes place over USB CDC serial port. This allows to use
native drivers in computer. Because communication takes place in 8bits and USB
splits data in 64-byte frames, own synchronization and own concept of *message*
in required.

## Packet structure

Packet consists of variable numbers of bytes. Packet structure is same in
both directions.

1. **Magic byte** `0x37`.
2. **Magic byte** `0xE2`.
3. **Command length byte** contains number of data bytes following (excluding
   this packet).
4. **Command code byte**.
5. **Data bytes**. Up to 122 data bytes (128 bytes buffer).

Message does not contain any checksum as checksum is handled by USB bus
natively.


## PC → DC-01 <a name="pctodc01"></a>

### `0x10` DC-01 Information Request <a name="pm-info"></a>

* Request to send general info about DC-01.
* Command Code byte: `0x10`.
* Standard abbreviation: `DC_PM_INFO_REQ`.
* N.o. data bytes: 0.
* Response: [*DC-01 Information*](#mp-info).

### `0x11` Set DCC state <a name="pm-setstate"></a>

* Set state of DCC cutter.
* Command Code byte: `0x11`.
* Standard abbreviation: `DC_PM_SET_STATE`.
* N.o. data bytes: 1.
  - 0: `0b0000000s`; `s`: if DCC should be on or off.
* Response: [*DC-01 State*](#mp-state).
* Packet with `s=1` must be sent to device each 200 ms (timeout 500 ms)
  to assure DCC is on. In case of timeout, DC-01 cuts DCC.


## DC-01 → PC <a name="dc01topc"></a>

### `0x10` DC-01 Information <a name="mp-info"></a>

* Report general information about DC-01.
* Command Code byte: `0x20`.
* Standard abbreviation: `DC_MP_INFO`.
* Data bytes:
  0. Firmware version major
  1. Firmware version minor
* In response to: [*DC-01 Information Request*](#pm-info).

### `0x11` DC-01 state <a name="mp-state></a>

* Report general state of DC-01.
* Command Code byte: `0x11`.
* Standard abbreviation: `DC_MP_STATE`.
* N.o. data bytes: 2.
  - See [operation.md](operation.md) for bytes description.
* This packet is sent to PC automatically each 500 ms.

### `0x12` DC-01 Big relay test status <a name="mp-brstatus></a>

* Report state of DC-01 Big relay test.
* Command Code byte: `0x12`.
* Standard abbreviation: `DC_MP_BRSTATE`.
* N.o. data bytes: 3.
  - See [operation.md](operation.md) for bytes description.
* This packet is sent to PC automatically each 500 ms.
