# Implementation details

## How it works

```
CONFIG.SYS ─ init ─┬─ usb.c    power the port, USB reset, enumerate,
                   │            find the bulk-only mass storage interface
                   ├─ scsi.c   Bulk Only Transport: CBW → data → CSW,
                   │            INQUIRY / TEST UNIT READY / READ CAPACITY
                   └─ volume.c partition table → one Human68k BPB per volume

Human68k read/write ─ msd.c ─ volume.c ─ scsi.c ─ usb.c
```

- `driver.s` — device header, strategy and interrupt entries. Must be linked
  first so the header lands at offset 0 of the image.
- `usb.c` — SL811HS host controller: transactions, control transfers, bulk
  transfers, enumeration.
- `scsi.c` — USB Bulk Only Transport and the SCSI commands.
- `volume.c` — bringing the medium up, partition table and BPB handling.
- `msd.c` — the Human68k side: request packet dispatch, read/write.
- `print.c`, `lowlevel.s` — output through DOS `_PRINT`, and the byte pump
  in and out of the SL811's buffer RAM.

### Why this does not sit on top of USBDRV.SYS

`USBDRV.SYS` exports a nice IOCS `$F4` interface, and `USBFDD.SYS` uses it,
but its bulk transfer routines always start a transfer at DATA0. That works
for a USB floppy, whose data transfers are always a whole number of 512 byte
sectors — an even number of 64 byte packets, so the toggle always comes back
round to DATA0 by itself. Bulk Only Transport sends a 31 byte command block
and reads a 13 byte status block, one packet each, so after the first command
the toggle is out of step and the device silently ignores everything that
follows.

Keeping the toggles is therefore this driver's job, which is why it talks to
the SL811HS registers directly. The chip start-up sequence, the SOF setup and
the "does this transfer still fit in the current frame" test are taken from
`USBDRV.SYS`, since those are known to work on this hardware.

## Known unknowns

Things worth suspecting first if something misbehaves:

1. **The 32 bit BPB size field.** For volumes over 65535 sectors the driver
   sets the 16 bit size field to zero and fills in the 32 bit one, which is
   what the Human68k DDK documents. If Human68k on your machine ignores it, a
   large volume will look empty or wrong — check with a small FAT16 partition
   before blaming anything else. Nothing is written unless you write to the
   drive, and `-r` makes that impossible.
2. **Sectors per FAT is a single byte** in the Human68k BPB. Partitions whose
   FAT is larger than 255 sectors (roughly: FAT16 with more than ~65000
   clusters) are skipped with a message. Reformatting with a larger cluster
   size fixes it.
3. **Device detection.** `usb_hw_init()` reports what the SL811 interrupt
   status register said at power-up (`SL811 status 0xNN` in the error
   message). Detection is not based on it — enumeration decides — but the
   value is a useful clue.
4. **Hot plugging is not supported.** Media check always answers "not
   changed". Swapping sticks while running will confuse Human68k's cache.

