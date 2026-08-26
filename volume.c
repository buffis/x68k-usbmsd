/*
 *	volume.c - get the memory stick talking, then work out which FAT
 *		   volumes on it Human68k can be told about
 */

#include "volume.h"
#include "scsi.h"
#include "print.h"

struct unit vol_units[MAX_UNITS] = {
	{ 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};
int vol_nunits = 0;
u32 vol_lastlba = 0;
u8 vol_secbuf[SECT_SIZE] = { 0 };
int vol_verbose = 0;

/* Human68k reads the BPB straight out of our memory, so the layout has to
 * be exactly right.  Break the build if the compiler ever pads it. */
typedef char bpb_layout_check[
	(sizeof(struct bpb) == 16 &&
	 __builtin_offsetof(struct bpb, nsize) == 8 &&
	 __builtin_offsetof(struct bpb, nfsect) == 11 &&
	 __builtin_offsetof(struct bpb, huge) == 12) ? 1 : -1];

/* ------------------------------------------------------------------ *
 *  Little endian helpers for on-disk structures
 * ------------------------------------------------------------------ */

static u16 le16(const u8 *p)
{
	return (u16)(p[0] | (p[1] << 8));
}

static u32 le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/* ------------------------------------------------------------------ *
 *  Disk I/O with retries
 * ------------------------------------------------------------------ */

int vol_io(int is_write, u32 lba, u16 count, void *buf)
{
	int tries;

	for (tries = 0; tries < 3; tries++) {
		int r = is_write ? scsi_write10(lba, count, buf)
				 : scsi_read10(lba, count, buf);

		if (r == SCSI_OK)
			return 0;

		if (r == SCSI_FAIL) {
			scsi_request_sense();
			switch (scsi_sense[2] & 0x0F) {
			case 0x02:			/* not ready */
				scsi_wait_ready(3);
				break;
			case 0x06:			/* unit attention */
				break;			/* just try again */
			case 0x07:			/* write protected */
				return -1;
			default:
				break;
			}
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ *
 *  Bringing the device up
 * ------------------------------------------------------------------ */

static void print_inquiry(void)
{
	u8 inq[36];
	int i;

	for (i = 0; i < 36; i++)
		inq[i] = 0;

	if (scsi_inquiry(inq) != SCSI_OK)
		return;

	pstr("  ");
	for (i = 8; i < 32; i++) {		/* vendor + product strings */
		char c = (char)inq[i];

		if (c < ' ' || c > '~')
			c = ' ';
		pchar(c);
	}
	pflush();
}

int vol_probe(void)
{
	u32 lastlba, blksize;
	int r;

	r = usb_start();

	if (r != USB_OK) {
		switch (r) {
		case USB_ENODEV:
			pstr("  no USB device answering (SL811 status 0x");
			phex(usb_init_isr, 2);
			pstr(", then 0x");
			phex(usb_try_isr[0], 2);
			pchar('/');
			phex(usb_try_isr[1], 2);
			pchar('/');
			phex(usb_try_isr[2], 2);
			pchar(')');
			break;
		case USB_EUNSUP:
			pstr("  not a bulk-only mass storage device");
			break;
		case USB_ESTALL:
			pstr("  device refused a standard request");
			break;
		default:
			pstr("  USB error ");
			pdec((u32)(-r));
			break;
		}
		pflush();
		return -1;
	}

	if (vol_verbose) {
		pstr("  device ");
		phex(usb.vid, 4);
		pchar(':');
		phex(usb.pid, 4);
		pstr("  bulk in ");
		pdec(usb.ep_in);
		pstr(" out ");
		pdec(usb.ep_out);
		pstr("  maxpacket ");
		pdec(usb.in_size);
		pstr("  subclass 0x");
		phex(usb.subclass, 2);
		pflush();
	}

	bot_read_max_lun();

	if (scsi_wait_ready(10) != SCSI_OK) {
		pstr("  medium not ready (sense ");
		phex(scsi_sense[2] & 0x0F, 1);
		pchar('/');
		phex(scsi_sense[12], 2);
		pchar('/');
		phex(scsi_sense[13], 2);
		pchar(')');
		pflush();
		return -1;
	}

	print_inquiry();

	if (scsi_read_capacity(&lastlba, &blksize) != SCSI_OK) {
		pstr("  READ CAPACITY failed");
		pflush();
		return -1;
	}
	if (blksize != SECT_SIZE) {
		pstr("  unsupported sector size ");
		pdec(blksize);
		pflush();
		return -1;
	}
	vol_lastlba = lastlba;

	pstr("  capacity ");
	pdec((lastlba + 1) >> 11);		/* 512 byte sectors -> MB */
	pstr(" MB (");
	pdec(lastlba + 1);
	pstr(" sectors)");
	pflush();
	return 0;
}

/* ------------------------------------------------------------------ *
 *  FAT / partition table inspection
 * ------------------------------------------------------------------ */

#define FAT_NONE	0
#define FAT_OK		1
#define FAT_IS32	2

static int check_vbr(const u8 *s)
{
	u16 bps = le16(s + 11);
	u16 res = le16(s + 14);
	u16 root = le16(s + 17);
	u16 fatsz = le16(s + 22);

	if (bps != SECT_SIZE)
		return FAT_NONE;
	if (s[13] == 0)				/* sectors per cluster */
		return FAT_NONE;
	if (s[16] == 0 || s[16] > 4)		/* number of FATs */
		return FAT_NONE;
	if (res == 0)
		return FAT_NONE;
	if (root == 0 && fatsz == 0)
		return FAT_IS32;
	if (root == 0 || fatsz == 0)
		return FAT_NONE;
	return FAT_OK;
}

/*
 * Turn a FAT boot sector into a Human68k BPB.  Returns 0 on success.
 */
static int make_bpb(const u8 *s, u32 limit, struct bpb *b)
{
	u16 fatsz = le16(s + 22);
	u32 total = le16(s + 19);

	if (total == 0)
		total = le32(s + 32);
	if (total == 0)
		return -1;
	if (limit && total > limit)
		total = limit;

	if (fatsz > 255)			/* nfsect is only a byte */
		return -2;

	b->nbyte = SECT_SIZE;
	b->nsector = s[13];
	b->nfat = s[16];
	b->nreserved = le16(s + 14);
	b->ndirent = le16(s + 17);
	b->nsize = (total <= 0xFFFFUL) ? (u16)total : 0;
	b->mdesc = s[21];
	b->nfsect = (u8)fatsz;
	b->huge = total;
	return 0;
}

static int is_fat_parttype(u8 t)
{
	return (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0E);
}

static int is_fat32_parttype(u8 t)
{
	return (t == 0x0B || t == 0x0C);
}

/*
 * Does sector 0 hold a PC style partition table?  A FAT boot sector ends
 * in 55 aa as well, so the entries themselves have to look sane too.
 */
static int has_mbr(const u8 *s)
{
	int i, useful = 0;

	if (s[510] != 0x55 || s[511] != 0xAA)
		return 0;

	for (i = 0; i < 4; i++) {
		const u8 *e = s + 446 + i * 16;

		if (e[0] != 0x00 && e[0] != 0x80)
			return 0;
		if (e[4] != 0 && le32(e + 12) != 0)
			useful = 1;
	}
	return useful;
}

static void add_unit(u32 start, u32 nsect, const u8 *vbr, int index)
{
	struct unit *u = &vol_units[vol_nunits];
	int r = make_bpb(vbr, nsect, &u->bpb);

	if (r) {
		pstr("  partition ");
		pdec((u32)index);
		pstr(r == -2 ? ": FAT is too big for Human68k, skipped"
			     : ": broken boot sector, skipped");
		pflush();
		return;
	}

	u->start = start;
	u->nsect = u->bpb.nsize ? (u32)u->bpb.nsize : u->bpb.huge;
	vol_nunits++;
}

void vol_scan(void)
{
	u8 ptab[64];
	int i;

	if (vol_io(0, 0, 1, vol_secbuf)) {
		pstr("  cannot read sector 0");
		pflush();
		return;
	}

	if (!has_mbr(vol_secbuf)) {
		int t = check_vbr(vol_secbuf);

		if (t == FAT_OK) {
			if (vol_verbose) {
				pstr("  no partition table, using the whole medium");
				pflush();
			}
			add_unit(0, vol_lastlba + 1, vol_secbuf, 0);
		} else if (t == FAT_IS32) {
			pstr("  medium is FAT32, which Human68k cannot read");
			pflush();
		} else {
			pstr("  neither a partition table nor a FAT boot sector");
			pflush();
		}
		return;
	}

	for (i = 0; i < 64; i++)
		ptab[i] = vol_secbuf[446 + i];

	for (i = 0; i < 4; i++) {
		const u8 *e = ptab + i * 16;
		u8 type = e[4];
		u32 start = le32(e + 8);
		u32 size = le32(e + 12);
		int t;

		if (type == 0 || size == 0)
			continue;

		if (is_fat32_parttype(type)) {
			pstr("  partition ");
			pdec((u32)(i + 1));
			pstr(": FAT32, which Human68k cannot read");
			pflush();
			continue;
		}
		if (!is_fat_parttype(type)) {
			if (vol_verbose) {
				pstr("  partition ");
				pdec((u32)(i + 1));
				pstr(": type 0x");
				phex(type, 2);
				pstr(", ignored");
				pflush();
			}
			continue;
		}
		if (vol_nunits >= MAX_UNITS)
			break;

		if (vol_io(0, start, 1, vol_secbuf)) {
			pstr("  partition ");
			pdec((u32)(i + 1));
			pstr(": boot sector unreadable");
			pflush();
			continue;
		}
		t = check_vbr(vol_secbuf);
		if (t != FAT_OK) {
			pstr("  partition ");
			pdec((u32)(i + 1));
			pstr(t == FAT_IS32 ? ": FAT32, which Human68k cannot read"
					   : ": not a FAT volume");
			pflush();
			continue;
		}
		add_unit(start, size, vol_secbuf, i + 1);
	}
}
