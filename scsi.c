/*
 *	scsi.c - USB Bulk Only Transport (BBB) and the SCSI commands used
 *		 to talk to a USB memory stick
 *
 *	Every transfer is:  CBW out -> optional data -> CSW in.
 *	Numbers inside the CBW/CSW are little endian, numbers inside a SCSI
 *	command block are big endian, so both are assembled byte by byte.
 */

#include "scsi.h"

u8 scsi_sense[18] = { 0 };
u8 scsi_maxlun = 0;
u32 scsi_residue = 0;		/* bytes the last command did not transfer */

static u8 cbw[31] = { 0 };
static u8 csw[13] = { 0 };
static u32 cbw_tag = 0;
static u8 lun = 0;

/* ------------------------------------------------------------------ *
 *  Class specific requests
 * ------------------------------------------------------------------ */

int bot_reset(void)
{
	int r = usb_control(0x21, 0xFF, 0, usb.iface, 0, 0);

	delay_ms(50);
	/* Whatever the device thought about toggles, they are DATA0 again
	 * once the halts are cleared. */
	usb_clear_halt((u8)(0x80 | usb.ep_in));
	usb_clear_halt(usb.ep_out);
	usb.tog_in = 0;
	usb.tog_out = 0;
	return (r < 0) ? SCSI_ERR : SCSI_OK;
}

int bot_read_max_lun(void)
{
	u8 buf[1];
	int r;

	buf[0] = 0;
	r = usb_control(0xA1, 0xFE, 0, usb.iface, 1, buf);
	if (r < 0) {
		/* Devices with a single LUN are allowed to stall this, and a
		 * stalled control endpoint clears itself on the next SETUP. */
		scsi_maxlun = 0;
		return SCSI_OK;
	}
	scsi_maxlun = (buf[0] > 15) ? 0 : buf[0];
	return SCSI_OK;
}

/* ------------------------------------------------------------------ *
 *  One Bulk Only command
 * ------------------------------------------------------------------ */

static int bot_command(const u8 *cdb, int cdblen, void *data, u32 len,
		       int dir_in)
{
	int i, r;

	if (cdblen > 16)
		return SCSI_ERR;

	scsi_residue = len;

	cbw_tag++;

	cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43; /* "USBC" */
	cbw[4] = (u8)cbw_tag;
	cbw[5] = (u8)(cbw_tag >> 8);
	cbw[6] = (u8)(cbw_tag >> 16);
	cbw[7] = (u8)(cbw_tag >> 24);
	cbw[8] = (u8)len;
	cbw[9] = (u8)(len >> 8);
	cbw[10] = (u8)(len >> 16);
	cbw[11] = (u8)(len >> 24);
	cbw[12] = (u8)(dir_in ? 0x80 : 0x00);
	cbw[13] = lun;
	cbw[14] = (u8)cdblen;
	for (i = 0; i < 16; i++)
		cbw[15 + i] = (i < cdblen) ? cdb[i] : 0;

	/* --- command phase --- */
	r = usb_bulk_out(cbw, 31);
	if (r != 31) {
		if (r == USB_ESTALL)
			usb_clear_halt(usb.ep_out);
		bot_reset();
		return SCSI_ERR;
	}

	/* --- data phase --- */
	if (len) {
		r = dir_in ? usb_bulk_in(data, len) : usb_bulk_out(data, len);
		if (r == USB_ESTALL) {
			/* A stalled data endpoint is recoverable: clear it
			 * and go collect the status. */
			usb_clear_halt((u8)(dir_in ? (0x80 | usb.ep_in)
						   : usb.ep_out));
		} else if (r < 0) {
			bot_reset();
			return SCSI_ERR;
		}
	}

	/* --- status phase --- */
	r = usb_bulk_in(csw, 13);
	if (r == USB_ESTALL) {
		usb_clear_halt((u8)(0x80 | usb.ep_in));
		r = usb_bulk_in(csw, 13);
	}
	if (r != 13) {
		bot_reset();
		return SCSI_ERR;
	}

	if (csw[0] != 0x55 || csw[1] != 0x53 ||		/* "USBS" */
	    csw[2] != 0x42 || csw[3] != 0x53) {
		bot_reset();
		return SCSI_ERR;
	}
	if (csw[4] != (u8)cbw_tag || csw[5] != (u8)(cbw_tag >> 8) ||
	    csw[6] != (u8)(cbw_tag >> 16) || csw[7] != (u8)(cbw_tag >> 24)) {
		bot_reset();
		return SCSI_ERR;
	}

	scsi_residue = (u32)csw[8] | ((u32)csw[9] << 8) |
		       ((u32)csw[10] << 16) | ((u32)csw[11] << 24);

	if (csw[12] == 0)
		return SCSI_OK;
	if (csw[12] == 1)
		return SCSI_FAIL;

	bot_reset();
	return SCSI_PHASE;
}

/* ------------------------------------------------------------------ *
 *  SCSI commands
 * ------------------------------------------------------------------ */

static u8 cdb[12] = { 0 };

static void cdb_clear(void)
{
	int i;

	for (i = 0; i < 12; i++)
		cdb[i] = 0;
}

int scsi_test_unit_ready(void)
{
	cdb_clear();
	cdb[0] = 0x00;
	return bot_command(cdb, 6, 0, 0, 0);
}

int scsi_request_sense(void)
{
	int i, r;

	for (i = 0; i < 18; i++)
		scsi_sense[i] = 0;

	cdb_clear();
	cdb[0] = 0x03;
	cdb[4] = 18;
	r = bot_command(cdb, 6, scsi_sense, 18, 1);
	return r;
}

int scsi_inquiry(u8 *buf36)
{
	cdb_clear();
	cdb[0] = 0x12;
	cdb[4] = 36;
	return bot_command(cdb, 6, buf36, 36, 1);
}

int scsi_start_unit(void)
{
	cdb_clear();
	cdb[0] = 0x1B;			/* START STOP UNIT */
	cdb[4] = 0x01;			/* start, no load/eject */
	return bot_command(cdb, 6, 0, 0, 0);
}

int scsi_read_capacity(u32 *last_lba, u32 *block_size)
{
	u8 buf[8];
	int r, i;

	for (i = 0; i < 8; i++)
		buf[i] = 0;

	cdb_clear();
	cdb[0] = 0x25;
	r = bot_command(cdb, 10, buf, 8, 1);
	if (r != SCSI_OK)
		return r;
	if (scsi_residue)
		return SCSI_FAIL;

	*last_lba = ((u32)buf[0] << 24) | ((u32)buf[1] << 16) |
		    ((u32)buf[2] << 8) | (u32)buf[3];
	*block_size = ((u32)buf[4] << 24) | ((u32)buf[5] << 16) |
		      ((u32)buf[6] << 8) | (u32)buf[7];
	return SCSI_OK;
}

int scsi_read10(u32 lba, u16 count, void *buf)
{
	int r;

	cdb_clear();
	cdb[0] = 0x28;
	cdb[2] = (u8)(lba >> 24);
	cdb[3] = (u8)(lba >> 16);
	cdb[4] = (u8)(lba >> 8);
	cdb[5] = (u8)lba;
	cdb[7] = (u8)(count >> 8);
	cdb[8] = (u8)count;
	r = bot_command(cdb, 10, buf, (u32)count << 9, 1);
	/* A good status with data missing is still a failed read */
	if (r == SCSI_OK && scsi_residue)
		return SCSI_FAIL;
	return r;
}

int scsi_write10(u32 lba, u16 count, const void *buf)
{
	int r;

	cdb_clear();
	cdb[0] = 0x2A;
	cdb[2] = (u8)(lba >> 24);
	cdb[3] = (u8)(lba >> 16);
	cdb[4] = (u8)(lba >> 8);
	cdb[5] = (u8)lba;
	cdb[7] = (u8)(count >> 8);
	cdb[8] = (u8)count;
	r = bot_command(cdb, 10, (void *)buf, (u32)count << 9, 0);
	if (r == SCSI_OK && scsi_residue)
		return SCSI_FAIL;
	return r;
}

/* ------------------------------------------------------------------ *
 *  Getting a freshly plugged stick to admit it is ready
 * ------------------------------------------------------------------ */

int scsi_wait_ready(int seconds)
{
	int tries = seconds * 10;
	int started = 0;
	int broken = 0;
	int r;

	for (;;) {
		r = scsi_test_unit_ready();
		if (r == SCSI_OK)
			return SCSI_OK;

		if (r == SCSI_FAIL) {
			broken = 0;
			scsi_request_sense();
			/* 02/04/xx = not ready, becoming ready: just wait.
			 * 06/28/00 = medium changed: perfectly fine.
			 * 02/3a/xx = no medium at all: hopeless. */
			if ((scsi_sense[2] & 0x0F) == 0x02) {
				if (scsi_sense[12] == 0x3A)
					return SCSI_FAIL;
				if (scsi_sense[12] == 0x04 && !started) {
					started = 1;
					scsi_start_unit();
				}
			}
		} else if (r == SCSI_ERR) {
			/* Transport hiccup.  bot_command has already reset
			 * the device, so a couple of retries are worth it. */
			if (++broken >= 3)
				return SCSI_ERR;
		}

		if (--tries <= 0)
			return SCSI_FAIL;
		delay_ms(100);
	}
}
