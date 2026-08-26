/*
 *	msd.c - Human68k block device driver for USB memory sticks
 *		plugged into a Nereid card
 *
 *	One unit is created per usable FAT partition (or one unit for the
 *	whole medium if it is formatted without a partition table), so the
 *	stick simply shows up as one or more drive letters.
 */

#include "usb.h"
#include "scsi.h"
#include "ddk.h"
#include "volume.h"
#include "print.h"

#define VERSION	"0.1"

#define MAX_XFER_SECT	32		/* sectors per SCSI command */

static struct bpb *bpb_ptr[MAX_UNITS] = { 0, 0, 0, 0 };
static int readonly = 0;
static int online = 0;			/* did init succeed? */

/* provided by driver.s / zzend.s */
extern char drv_header;
extern char drv_text_end;
extern char drv_data_end;

/* ------------------------------------------------------------------ *
 *  Init
 * ------------------------------------------------------------------ */

static void parse_args(const char *p)
{
	int n;

	if (!p)
		return;
	for (n = 0; n < 128 && p[n]; n++) {
		if (p[n] != '-' && p[n] != '/')
			continue;
		switch (p[n + 1]) {
		case 'v': case 'V':
			vol_verbose = 1;
			break;
		case 'r': case 'R':
			readonly = 1;
			break;
		default:
			break;
		}
	}
}

static int cmd_init(u8 *rp)
{
	u32 endaddr;
	u8 drive;
	int i;

	/* The argument pointer and the drive number must be read before the
	 * reply fields are written, they share the same bytes. */
	parse_args(*(char **)(rp + 18));
	drive = rp[22];

	pstr("[USBMSD.SYS] USB mass storage driver for Nereid v" VERSION);
	pflush();

	if (vol_probe())
		goto fail;

	vol_scan();

	if (vol_nunits == 0) {
		pstr("  no usable FAT12/FAT16 volume found");
		pflush();
		goto fail;
	}

	for (i = 0; i < vol_nunits; i++)
		bpb_ptr[i] = &vol_units[i].bpb;

	endaddr = (u32)&drv_data_end;
	if ((u32)&drv_text_end > endaddr)
		endaddr = (u32)&drv_text_end;

	for (i = 0; i < vol_nunits; i++) {
		pstr("  ");
		pchar((char)('A' + drive + i));
		pstr(": ");
		pdec(vol_units[i].nsect >> 11);
		pstr(" MB, ");
		pdec((u32)vol_units[i].bpb.nsector);
		pstr(" sectors/cluster, from LBA ");
		pdec(vol_units[i].start);
		if (readonly)
			pstr(", read only");
		pflush();
	}

	online = 1;
	rp[13] = (u8)vol_nunits;
	*(u32 *)(rp + 14) = endaddr;
	*(u32 *)(rp + 18) = (u32)bpb_ptr;
	return E_OK;

fail:
	pstr("  driver not installed");
	pflush();
	rp[13] = 0;
	*(u32 *)(rp + 14) = (u32)&drv_header;	/* keep nothing resident */
	*(u32 *)(rp + 18) = (u32)bpb_ptr;
	return E_OK;
}

/* ------------------------------------------------------------------ *
 *  Normal operation
 * ------------------------------------------------------------------ */

static int cmd_media_check(u8 *rp)
{
	rp[14] = M_NOT_CHANGED;
	return E_OK;
}

static int cmd_build_bpb(u8 *rp)
{
	int unit = rp[1];

	if (unit >= vol_nunits)
		return S_ABORT | S_IGNORE | E_UNIT;
	*(u32 *)(rp + 18) = (u32)&vol_units[unit].bpb;
	return E_OK;
}

static int cmd_drvctl(u8 *rp)
{
	u8 st = DS_EJECT_UNAVAILABLE | DS_MEDIA_INSERTED;

	if (readonly)
		st |= DS_WRITE_PROTECTED;
	if (!online)
		st |= DS_DRIVE_NOT_READY;
	rp[13] = st;
	return E_OK;
}

static int cmd_rw(u8 *rp, int is_write)
{
	int unit = rp[1];
	u8 *buf = *(u8 **)(rp + 14);
	u32 count = *(u32 *)(rp + 18);
	u32 start = *(u32 *)(rp + 22);
	u32 lba;

	if (!online)
		return S_ABORT | S_IGNORE | E_NOTRDY;
	if (unit >= vol_nunits)
		return S_ABORT | S_IGNORE | E_UNIT;
	if (is_write && readonly)
		return S_ABORT | S_IGNORE | E_WRPRT;
	if (start > vol_units[unit].nsect ||
	    count > vol_units[unit].nsect - start)
		return S_ABORT | S_IGNORE | E_NOTFND;
	if (count == 0)
		return E_OK;

	lba = vol_units[unit].start + start;
	while (count) {
		u16 n = (count > MAX_XFER_SECT) ? MAX_XFER_SECT : (u16)count;

		if (vol_io(is_write, lba, n, buf))
			return S_ABORT | S_RETRY | S_IGNORE |
			       (is_write ? E_WRITE : E_READ);
		lba += n;
		buf += (u32)n << 9;
		count -= n;
	}
	return E_OK;
}

/* ------------------------------------------------------------------ *
 *  Entry point, called from the interrupt routine in driver.s
 * ------------------------------------------------------------------ */

int msd_dispatch(u8 *rp);

int msd_dispatch(u8 *rp)
{
	switch (rp[2]) {
	case C_INIT:
		return cmd_init(rp);
	case C_MEDIACHK:
		return cmd_media_check(rp);
	case C_BLDBPB:
		return cmd_build_bpb(rp);
	case C_INPUT:
		return cmd_rw(rp, 0);
	case C_OUTPUT:
	case C_OUTVFY:
		return cmd_rw(rp, 1);
	case C_DRVCTL:
		return cmd_drvctl(rp);
	case C_IOCTLIN:
	case C_IOCTLOUT:
		return E_OK;
	default:
		return S_ABORT | S_IGNORE | E_CMD;
	}
}
