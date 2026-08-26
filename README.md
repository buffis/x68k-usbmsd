# USBMSD.SYS - USB memory stick driver for the Nereid card

A driver that makes a USB memory stick plugged into a Nereid's USB port
appear as hard drives on an X68000.

Consists of two parts:

| file | description |
|------|-------------|
| `USBMSD.SYS` | The driver, loaded from `CONFIG.SYS` |
| `USBTEST.X`  | Test utility |

## Installing

Copy `USBMSD.SYS` to the boot drive and add a line to `CONFIG.SYS`:

```
DEVICE = ¥USBMSD.SYS -v
```

Options:

- `-v` : verbose: print info about the USB drive
- `-r` : mount as read only

The stick must be plugged in before the machine boots.

At boot it prints something like:

```
[USBMSD.SYS] USB mass storage driver for Nereid v0.1
  SanDisk  Cruzer Blade
  capacity 1948 MB (3991552 sectors)
  D: 31 MB, 4 sectors/cluster, from LBA 63
```

`USBMSD.SYS` does **not** need `USBDRV.SYS`, and the two should NOT be loaded
together, since `USBDRV.SYS` installs a timer interrupt handler that writes
to the same chip registers.

## Requirements on the stick

- **Format the stick FAT16** (`FAT` in Windows' format dialog, or
  `mkfs.vfat -F 16` on Linux).
- At most four partitions with maximum size 1GB each.
- 512 byte sectors.

A first test is easiest with a small partition. A FAT16 volume of 32 MB or
less is described entirely by the classic 16 bit BPB fields. Once you have that
working, you can try bigger sizes.

## USBTEST.X (test utility)

Run it from the command line. This is the thing to reach for when the driver
does not work out of the box.

```
USBTEST         probe the stick, list the volumes and the BPB of each
USBTEST 0       the same, plus a hex dump of sector 0 (the partition table)
USBTEST 63      hex dump of sector 63 (a typical first partition's boot sector)
```

It always runs in verbose mode and it re-powers and re-enumerates the port
every time, so it can be run repeatedly and with the stick swapped in
between. It does not write anything.

## Building

- Install [xdev68k](https://github.com/yosshin4004/xdev68k)
- `export XDEV68K_DIR=/path/to/xdev68k`
- `make`

Both binaries end up in `bin/`.

### Notes on the build

Human68k loads a `.SYS` driver as the image it finds on disk, and the driver
tells it how much of that image to keep resident. Two consequences the
makefile enforces:

- **No BSS.** Everything is compiled with `-fno-zero-initialized-in-bss
  -fno-common` so that even zero-initialised variables end up in `.data`, and
  the link step aborts if a BSS section appeared anyway.
- **The end of the image must be known.** `zzend.s` is linked last and marks
  it; the link step cross-checks that marker against the linker map, because
  library code pulled in from `libgcc.a` is placed after it.
