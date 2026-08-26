/*
 *	test.c - USBTEST.X, the diagnostic twin of USBMSD.SYS
 *
 *	Does exactly what the driver does at boot time, but from the command
 *	line and with everything it learns printed out - much nicer than
 *	editing CONFIG.SYS and rebooting after every experiment.
 *
 *	  usbtest		probe the stick and list its volumes
 *	  usbtest <sector>	also dump that sector of the raw medium
 */

#include "usb.h"
#include "scsi.h"
#include "ddk.h"
#include "volume.h"
#include "print.h"

static int have_dump = 0;
static u32 dump_lba = 0;

static void parse_cmdline(const char *p)
{
	int i;

	for (i = 0; i < 120 && p[i]; i++) {
		if (p[i] >= '0' && p[i] <= '9') {
			u32 v = 0;

			while (p[i] >= '0' && p[i] <= '9')
				v = v * 10 + (u32)(p[i++] - '0');
			dump_lba = v;
			have_dump = 1;
			return;
		}
	}
}

static void dump_sector(u32 lba)
{
	int row, col;

	pstr("sector ");
	pdec(lba);
	pchar(':');
	pflush();

	if (vol_io(0, lba, 1, vol_secbuf)) {
		pstr("  read failed");
		pflush();
		return;
	}

	for (row = 0; row < SECT_SIZE; row += 16) {
		phex((u32)row, 3);
		pstr("  ");
		for (col = 0; col < 16; col++) {
			phex(vol_secbuf[row + col], 2);
			pchar(' ');
		}
		pchar(' ');
		for (col = 0; col < 16; col++) {
			char c = (char)vol_secbuf[row + col];

			pchar((c < ' ' || c > '~') ? '.' : c);
		}
		pflush();
	}
}

static void show_bpb(int i)
{
	struct bpb *b = &vol_units[i].bpb;

	pstr("  unit ");
	pdec((u32)i);
	pstr(": LBA ");
	pdec(vol_units[i].start);
	pstr(" + ");
	pdec(vol_units[i].nsect);
	pstr(" sectors (");
	pdec(vol_units[i].nsect >> 11);
	pstr(" MB)");
	pflush();

	pstr("    bytes/sector ");
	pdec(b->nbyte);
	pstr("  sectors/cluster ");
	pdec(b->nsector);
	pstr("  FATs ");
	pdec(b->nfat);
	pflush();

	pstr("    reserved ");
	pdec(b->nreserved);
	pstr("  root entries ");
	pdec(b->ndirent);
	pstr("  sectors/FAT ");
	pdec(b->nfsect);
	pflush();

	pstr("    media 0x");
	phex(b->mdesc, 2);
	pstr("  size16 ");
	pdec(b->nsize);
	pstr("  size32 ");
	pdec(b->huge);
	pflush();
}

/*
 * Is the card there, and does the stick pull D+ up when it gets power?
 * These two answers between them say whether a failure is the card, the
 * power/device, or the protocol.
 */
static void check_hardware(void)
{
	u8 rev = 0;
	u8 isr = 0;
	int ok = usb_chip_test(&rev);
	int i;

	pstr("  SL811 revision ");
	phex(rev, 1);
	pstr(ok ? ", buffer RAM OK" : ", BUFFER RAM TEST FAILED");
	pflush();

	if (!ok) {
		pstr("  the card is not answering at $ECE381 - nothing else");
		pflush();
		pstr("  below will mean anything");
		pflush();
		return;
	}

	usb_hw_init(300);

	for (i = 0; i < 30; i++) {
		isr = usb_reg_rd(SL_ISR);
		if (isr & IS_DPLUS)
			break;
		delay_ms(100);
	}

	pstr("  port: ");
	if (isr & IS_DPLUS) {
		pstr("D+ high after ");
		pdec((u32)i * 100);
		pstr(" ms");
	} else {
		pstr("D+ still low after 3 s - the device is not powering up");
	}
	pstr(" (status 0x");
	phex(isr, 2);
	pchar(')');
	pflush();
}

int test_main(const char *cmdline);

int test_main(const char *cmdline)
{
	int i;

	parse_cmdline(cmdline);
	vol_verbose = 1;

	pstr("USBTEST - Nereid USB mass storage probe");
	pflush();

	check_hardware();

	if (vol_probe())
		return 1;

	vol_scan();

	if (vol_nunits == 0) {
		pstr("  no volume Human68k could mount");
		pflush();
	} else {
		for (i = 0; i < vol_nunits; i++)
			show_bpb(i);
	}

	if (have_dump)
		dump_sector(dump_lba);

	return 0;
}
