DC-01 ↔ PC Device Operation Description
=======================================

## DC-01's state

`0b0MMM0NFC 0xEE`

* `MM`: mode
   - 0: initialization
   - 1: normal operation
   - 2: Big relay test
   - 3: override
   - 4: failure
* `C`: DCC is connected
* `I`: DCC on input present
* `E`: failure code
  - 0: no failure
  - 1: Big relay test failure
  - 2: Continuous test failure

## Internal checks

### Big relay test

Test is performed every time input DCC appears and on startup. Consequently,
when input DCC is lot, DC-01 cuts DCC so it can run test when DCC appears.

Test sequence:

1. Turn off both relays.
2. Wait for single side to be active.
  - If not deactivated within 1 second → error XX.
3. Determine input & output side.
4. Activate both relays.
5. Check DCC is present on output.
  - If output DCC does not appear within 1 s → error XX.
6. Turn off relay 1.
7. Check DCC is absent on output.
  - If output DCC does not disappear within 1 s → error XX.
8. Turn on relay 1.
9. Check DCC is present on output.
  - If output DCC does not disappear within 1 s → error XX.
10. Turn off relay 2.
11. Check DCC is absent on output.
  - If output DCC does not disappear within 1 s → error XX.
12. Turn on relay 2.
13. Check DCC is present on output.
  - If output DCC does not disappear within 1 s → error XX.
14. Set relays to desired state.
15. *Test finished.*

* If both DCC's are lost at any time in test, test is stopped. When DCC appears,
  whole test in run again.
* Test cannot be run in override mode – if switched to override mode during test,
  test is interrupted.

#### Test status

Big relay test status consists of 3 bytes.

`0x0S 0xTT 0xEE`

* `S`: test state
  - 0: test not yet run
  - 1: test in progress
  - 2: test successful
  - 3: test fail (error present in `EE`)
  - 4: test interrupted due to DCC absence
* `TT`: last reached step of test
* `EE`: error reported during test (0 = no error)

### Continuous test

In normal operation mode and in override mode

### DCC measuring

DCC detection must be immune to RailCom cutouts. This means interrupt in DCC
lasting 500 us must be still considered as active DCC. Shortest DCC packet
lasts ~2400 us.

DC-01 detects DCC in sliding window of length 2 ms. Each 50 us DCC is detected.
40 samples are in window each time. DCC is considered as active when DCC is active
in >= 15 samples.
