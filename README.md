# EEPROM Programmer (Host Software)
This is the host software for an LPC51U68-based parallel EEPROM programmer, [eeprog-fw-lpc][eeprog]. It's not perfect, but most issues can be resolved by restarting the programmer, or disconnecting and reconnecting the programmer.

## Building:
Run `make`

## Usage:
```
./eeprog FILE [-d|--dump|--upload] [-i|--input=INPUTFILE] [-a|--address=START] [-s|--skip=SKIP] [-c|--count=COUNT] [-o|--output=OUTFILE] [-v|--verify] [-r|--retain]
cat dataToUpload.bin | ./eeprog FILE
./eeprog FILE < dataToUpload.bin
./eeprog FILE > dumpedData.bin
```
| Flag          | Description |
|---------------|-------------|
| FILE          | Path to the device file for the programmer's serial port. Usually `/dev/ttyS[n]`. I recommend using [`FindUsbTty`][findusbtty] (e.g. ``./eeprog `FindUsbTty 1fc9 0090` ``) (which I 100% expect a similar tool already exists, but it was so simple I figured I'd make it anyway) |
| -d\|--dump    | Extracts and dumps the currently flashed data to a given file or STDOUT. |
| --upload      | Uploads (flashes) the EEPROM with the supplied data. |
| -a\|--address | Specifies the start address of the EEPROM to start dumping from/uploading to. |
| -s\|--skip    | When uploading, specifies the amount of data to skip from the source file. |
| -c\|--count   | Specifies the amount of data to dump/upload. |
| -i\|--input   | Specifies the input file to use when uploading. |
| -o\|--output  | Specifies the output file to use when dumping. |
| -v\|--verify  | Verifies data that is written. |
| -r\|--retain  | Retains any data that lies within blocks of the given data range but that is outside of the data range itself. This is important because the EEPROM gets written in blocks of a certain size, and any unspecified data will be erased. |


[eeprog]: https://www.github.com/c4ashley/eeprog-fw-lpc
[findusbtty]: https://www.github.com/c4ashley/FindUsbTty
