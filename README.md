# EEPROM Programmer (Host Software)
This is the host software for an LPC51U68-based parallel EEPROM programmer, [eeprog-fw-lpc][eeprog]. It's not perfect, but most issues can be resolved by restarting the programmer, or disconnecting and reconnecting the programmer.

## Building:
Run `make`

## Usage:
```
./eeprog FILE [options]
cat dataToUpload.bin | ./eeprog FILE [options]
./eeprog FILE < dataToUpload.bin [options]
./eeprog FILE > dumpedData.bin [options]
```
| Flag                  | Description |
|-----------------------|-------------|
| FILE                  | Path to the device file for the programmer's serial port. Usually `/dev/ttyACM[n]` or `/dev/ttyS[n]`. I recommend using [`FindUsbTty`][findusbtty] (e.g. ``./eeprog `FindUsbTty 1fc9 0090` ``) (which I 100% expect a similar tool already exists, but it was so simple I figured I'd make it anyway) |
| -d\|--dump            | Extracts and dumps the currently flashed data to a given file or STDOUT. |
| -u\|--upload          | Uploads (flashes) the EEPROM with the supplied data. |
| -a\|--address=ADDRESS | Specifies the start address of the EEPROM to start dumping from/uploading to. |
| -s\|--skip=SKIP       | When uploading, specifies the amount of data to skip from the source file. |
| -c\|--count=COUNT     | Specifies the amount of data to dump/upload. |
| -i\|--input=PATH      | Specifies the input file to use when uploading. |
| -o\|--output=PATH     | Specifies the output file to use when dumping. |
| -v\|--verify          | Verifies data that is written. |
| -r\|--retain          | Retains any data that lies within blocks of the given data range but that is outside of the data range itself. This is important because the EEPROM gets written in blocks of a certain size, and any unspecified data will be erased. |
| -e\|--extended        | Treats the attached ROM as a 4Mbit (19-bit) ROM. [See below](#notes-on-extended-access) for details. |
| -p\|--page=N          | Selects the page to access on a 4Mbit ROM. [See below](#notes-on-extended-access) for details. |
| -b\|--baud=BAUD       | Set the baud rate to transfer data at. Set to 0 to use the TTY's current baud rate. The default is 115200. ***Note:*** *This will not change the baud rate of the programmer itself.* |
| --nostty              | Do not set the TTY attributes. This can be useful when the system has trouble setting the baud rate, for example. |


## Examples
Dump a 40-byte section of data starting at `0x400` into a file called `signature.rom`, using a SEGGER J-Link to communicate with the programmer:
```
eeprog `FindUsbTty 1366 1024` -a=0x400 -c0x28 > signature.rom
```

Read and echo a 12-byte string starting at `0x8c00`, using using an ST-Link to communicate with the programmer:
```
eeprog `FindUsbTty 0483 374b` --dump -a 0x8c00 -c 12
```

Print a hex-dump of a 256-bit sector starting at `0x1000`:
```
eeprog /dev/ttyACM0 --address 0x1000 --count 256 | hexdump -C
```

Upload and verify a null-terminated string to address `0x229`, making sure to retain other data within the sector:
```
echo -en "Hello, World!\x00" | eeprog /dev/ttyACM0 -uvr --address 0x229
```

Upload the entire contents of the file `data.rom` without verification or data retention:
```
eeprog /dev/ttyACM0 < data.rom
```

____________________________
#### Notes on Extended Access
- In the current iteration of the programmer, access is intended for devices up to 1Mbit (128KiB) devices, with a 17-bit address bus.
- 2Mbit (18-bit) devices can be programmed or read in pages by moving the jumper on the programmer board.
  - Setting the jumper towards the notch (pin 1 and 32) will tie address bit 17 HIGH, allowing access to addresses `0x20000~0x3FFFF`.
  - Setting the jumper away from the notch (towards pin 17 and 17) will tie address bit 17 LOW, allowing access to addresses `0x00000~0x1FFFF`.
  - Address ranges are still passed in software as `0x00000~0x1FFFF`.
- For 4Mbit (19-bit) devices, address bit 18 is shared with the R/W# pin on 18-bit and smaller devices, and instead use a high-voltage programming
  method which isn't yet supported.
  - Bit 18 can be selected by passing the --extended flag and using the --page=(0,1) argument.
  - Trying to write data will result in undefined behaviour.
  - Use the jumper pins as described above to set bit 17.
  - Address ranges are still passed in software as `0x00000~0x1FFFF`, so four separate reads will be required to dump an entire 4Mbit ROM.


[eeprog]: https://www.github.com/c4ashley/eeprog-fw-lpc
[findusbtty]: https://www.github.com/c4ashley/FindUsbTty
