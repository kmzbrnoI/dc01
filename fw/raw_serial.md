Debug commands to use raw communication with DC-01
==================================================

Read raw serial in linux:

```bash
$ stty -F /dev/ttyACM0 raw
$ xxd /dev/ttyACM0
```

Better way:

```bash
python3 -u read.py /dev/ttyACM0
```

Send to serial:

```bash
$ echo -ne '\x37\xE2\x02\x11\x01' > /dev/ttyACM0 # turn DCC on
```
