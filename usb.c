/*
 *	usb.c - SL811HS host controller driver + minimal USB core
 *
 *	The register level sequences (chip start up, SOF generation, the
 *	"is there enough time left in this frame" check) follow the original
 *	Nereid USB driver by Tachibana Eriko / Kuwajima Giken, which is known
 *	to work on this hardware.
 *
 *	What is *not* taken from it is the data toggle handling: this driver
 *	keeps the bulk toggles itself, which USB Bulk Only Transport requires
 *	(a 31 byte command block is a single packet, so the toggle does not
 *	conveniently return to DATA0 after every transfer).
 */

#include "usb.h"

struct usbdev usb = { 0, 8, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0 };

/* Scratch buffer for descriptors / setup packets. Everything in this driver
 * lives in .data on purpose - see the makefile, the image must have no BSS. */
static u8 setup_pkt[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static u8 descbuf[256] = { 0 };

/* ------------------------------------------------------------------ *
 *  Low level register access
 * ------------------------------------------------------------------ */

static void sl_wr(u8 reg, u8 val)
{
	SL811_ADDR_REG = reg;
	SL811_DATA_REG = val;
}

static u8 sl_rd(u8 reg)
{
	SL811_ADDR_REG = reg;
	return SL811_DATA_REG;
}

/* ------------------------------------------------------------------ *
 *  Timing
 *
 *  MFP GPIP bit 7 is the horizontal sync signal (~31kHz), the same trick
 *  the original driver uses.  Good enough for millisecond delays.
 * ------------------------------------------------------------------ */

void delay_ms(u32 ms)
{
	volatile u8 *gpip = (volatile u8 *)0xE88001;
	u32 edges = ms * 31UL;
	u8 prev = (u8)(*gpip & 0x80);

	while (edges) {
		u8 cur = (u8)(*gpip & 0x80);
		if (prev && !cur)
			edges--;
		prev = cur;
	}
}

/* ------------------------------------------------------------------ *
 *  One USB transaction
 *
 *  Returns USB_OK / USB_ESTALL / USB_ETIMEOUT / USB_EIO, or 1 for NAK.
 *  On a successful IN transaction *actual gets the number of bytes the
 *  device really sent, which may be less than requested.
 * ------------------------------------------------------------------ */

#define XACT_NAK	1

#define DONE_SPIN	400000L		/* ~1s worth of register polling */

static int sl_xact(u8 pid, u8 ep, int is_out, int toggle,
		   void *buf, int len, int *actual)
{
	u32 spin;
	u8 ctl, st, needed;

	sl_wr(SL_PIDEP, (u8)(pid | (ep & 0x0F)));
	sl_wr(SL_DEVADDR, usb.addr);
	sl_wr(SL_BUFADDR, SL_BUF_BASE);
	sl_wr(SL_BUFLEN, (u8)len);

	if (is_out && len) {
		SL811_ADDR_REG = SL_BUF_BASE;
		sl_write_buf(buf, len);
	}

	sl_wr(SL_ISR, 0xFF);			/* clear old interrupt flags */

	ctl = HC_ENABLE | HC_ARM;
	if (is_out)
		ctl |= HC_OUT;
	if (toggle)
		ctl |= HC_TOGGLE;

	/* Same "does this still fit in the current frame" test as the
	 * original driver.  One SOF timer unit is roughly eight byte times. */
	needed = (u8)((len >> 3) + 3);
	if (pid == PID_SETUP || needed <= sl_rd(SL_SOFTMR))
		ctl |= HC_AFTERSOF;

	sl_wr(SL_HOSTCTL, ctl);

	/* Wait for "transfer done" */
	for (spin = DONE_SPIN; spin; spin--) {
		if (sl_rd(SL_ISR) & IS_DONE_A)
			break;
	}
	if (!spin) {
		sl_wr(SL_HOSTCTL, 0);		/* disarm */
		return USB_ETIMEOUT;
	}

	st = sl_rd(SL_PKTSTAT);

	if (st & PS_ACK) {
		int got = len;
		if (!is_out) {
			int left = sl_rd(SL_XFERCNT);
			got = len - left;
			if (got < 0)
				got = 0;
			if (got) {
				SL811_ADDR_REG = SL_BUF_BASE;
				sl_read_buf(buf, got);
			}
		}
		if (actual)
			*actual = got;
		return USB_OK;
	}
	if (st & PS_TIMEOUT)
		return USB_ETIMEOUT;
	if (st & PS_NAK)
		return XACT_NAK;
	if (st & PS_STALL)
		return USB_ESTALL;
	return USB_EIO;
}

/*
 * A transaction with NAK retries.  Devices NAK while they are busy, which
 * during a big read can be quite often, so the limit is generous.
 */
static int sl_xact_retry(u8 pid, u8 ep, int is_out, int toggle,
			 void *buf, int len, int *actual, u32 naklimit)
{
	int r;
	int tries = 3;

	for (;;) {
		r = sl_xact(pid, ep, is_out, toggle, buf, len, actual);
		if (r == XACT_NAK) {
			if (naklimit == 0)
				return USB_ETIMEOUT;
			naklimit--;
			continue;
		}
		if (r == USB_ETIMEOUT || r == USB_EIO) {
			/* A retry costs nothing and cures the odd glitch */
			if (--tries > 0)
				continue;
		}
		return r;
	}
}

#define NAK_CTRL	20000L
#define NAK_BULK	200000L

/* ------------------------------------------------------------------ *
 *  Control transfers
 * ------------------------------------------------------------------ */

int usb_control(u8 type, u8 req, u16 value, u16 index, u16 len, void *data)
{
	u8 *p = (u8 *)data;
	int is_in = (type & 0x80) != 0;
	int done = 0;
	int toggle = 1;
	int r, act;

	setup_pkt[0] = type;
	setup_pkt[1] = req;
	setup_pkt[2] = (u8)value;		/* USB is little endian */
	setup_pkt[3] = (u8)(value >> 8);
	setup_pkt[4] = (u8)index;
	setup_pkt[5] = (u8)(index >> 8);
	setup_pkt[6] = (u8)len;
	setup_pkt[7] = (u8)(len >> 8);

	r = sl_xact_retry(PID_SETUP, 0, 1, 0, setup_pkt, 8, 0, NAK_CTRL);
	if (r != USB_OK)
		return r;

	while (done < (int)len) {
		int chunk = (int)len - done;
		if (chunk > usb.ep0size)
			chunk = usb.ep0size;
		act = 0;
		r = sl_xact_retry(is_in ? PID_IN : PID_OUT, 0, !is_in, toggle,
				  p + done, chunk, &act, NAK_CTRL);
		if (r != USB_OK)
			return r;
		toggle ^= 1;
		done += act;
		if (is_in && act < chunk)
			break;			/* short packet ends it */
	}

	/* Status stage always runs in the opposite direction, always DATA1 */
	r = sl_xact_retry(is_in ? PID_OUT : PID_IN, 0, is_in, 1,
			  setup_pkt, 0, 0, NAK_CTRL);
	if (r != USB_OK)
		return r;

	return done;
}

int usb_clear_halt(u8 ep_addr)
{
	int r = usb_control(0x02, 0x01, 0x0000, ep_addr, 0, 0);

	if (r < 0)
		return r;
	/* Clearing the halt feature also resets that endpoint's data toggle */
	if ((ep_addr & 0x80) && (ep_addr & 0x0F) == usb.ep_in)
		usb.tog_in = 0;
	else if (!(ep_addr & 0x80) && (ep_addr & 0x0F) == usb.ep_out)
		usb.tog_out = 0;
	return USB_OK;
}

/* ------------------------------------------------------------------ *
 *  Bulk transfers
 *
 *  Returns the number of bytes transferred, or a negative error code.
 * ------------------------------------------------------------------ */

static int usb_bulk(int is_in, u8 *buf, u32 len)
{
	u32 done = 0;
	int maxp = is_in ? usb.in_size : usb.out_size;
	u8 ep = is_in ? usb.ep_in : usb.ep_out;
	int r, act;

	if (maxp <= 0 || maxp > SL_BUF_MAX)
		maxp = SL_BUF_MAX;

	while (done < len) {
		int chunk = (int)(len - done);
		if (chunk > maxp)
			chunk = maxp;
		act = 0;
		r = sl_xact_retry(is_in ? PID_IN : PID_OUT, ep, !is_in,
				  is_in ? usb.tog_in : usb.tog_out,
				  buf + done, chunk, &act, NAK_BULK);
		if (r != USB_OK)
			return r;
		if (is_in)
			usb.tog_in ^= 1;
		else
			usb.tog_out ^= 1;
		done += act;
		if (is_in && act < chunk)
			break;			/* short packet ends it */
	}
	return (int)done;
}

int usb_bulk_in(void *buf, u32 len)
{
	return usb_bulk(1, (u8 *)buf, len);
}

int usb_bulk_out(const void *buf, u32 len)
{
	return usb_bulk(0, (u8 *)buf, len);
}

/* ------------------------------------------------------------------ *
 *  Bringing the host controller and the port up
 * ------------------------------------------------------------------ */

u8 usb_init_isr = 0;		/* chip status right after power up */
u8 usb_try_isr[3] = { 0, 0, 0 };	/* and after each enumeration attempt */

u8 usb_reg_rd(u8 reg)
{
	return sl_rd(reg);
}

void usb_reg_wr(u8 reg, u8 val)
{
	sl_wr(reg, val);
}

/*
 * Is the SL811 there at all?  Reads the hardware revision and checks that
 * its buffer RAM remembers what it is told - if this fails the problem is
 * the card or its address, not USB.
 */
int usb_chip_test(u8 *rev)
{
	static const u8 pattern[4] = { 0x55, 0xAA, 0x00, 0xFF };
	int attempt, i;

	/* After a cold boot the card's USB section has never been switched
	 * on and the chip answers with zeroes, so enable it first. */
	NEREID_CTRL_REG = NC_HOST_STOP;
	delay_ms(50);

	for (attempt = 0; attempt < 2; attempt++) {
		int ok = 1;

		for (i = 0; i < 4; i++)
			sl_wr((u8)(SL_BUF_BASE + i), pattern[i]);
		for (i = 0; i < 4; i++) {
			if (sl_rd((u8)(SL_BUF_BASE + i)) != pattern[i])
				ok = 0;
		}
		*rev = (u8)(sl_rd(SL_HWREV) >> 4);
		if (ok)
			return 1;

		/* Second try with bus power on as well */
		NEREID_CTRL_REG = NC_HOST_STOP | NC_POWER;
		delay_ms(100);
	}
	return 0;
}

/*
 * Power cycle the port and start generating frames.
 *
 * The order here matters and is the one USBDRV.SYS uses: the SL811 only
 * becomes a host once bit 7 of control register 2 is set, so nothing may
 * be written to control register 1 before that.  While no SOF is being
 * generated the bus sits at SE0, which is what resets the device and puts
 * it back at address 0.
 */
int usb_hw_init(u32 settle_ms)
{
	/* Interrupts stay off for good: everything here is polled, and the
	 * SL811 must not fire while a transfer is in progress.  This also
	 * keeps a previously installed usbdrv.sys out of our way. */
	sl_wr(SL_IER, 0);

	NEREID_CTRL_REG = NC_HOST_STOP;			/* host halted */
	delay_ms(40);
	NEREID_CTRL_REG = NC_HOST_STOP | NC_POWER;	/* bus power on */
	delay_ms(40);

	sl_wr(SL_IER, 0);
	sl_wr(SL_ISR, 0xFF);
	delay_ms(settle_ms);				/* device sees SE0 */

	usb_init_isr = sl_rd(SL_ISR);

	/* Host mode, SOF counter 0x2e0, one frame per millisecond */
	sl_wr(SL_CTRL2, 0xAE);
	sl_wr(SL_SOFLOW, 0xE0);
	sl_wr(SL_CTRL1, C1_RUN);

	/* Hand the SOF token to the transmit engine and start it */
	sl_wr(SL_PIDEP, PID_SOF);
	sl_wr(SL_DEVADDR, 0);
	sl_wr(SL_HOSTCTL, HC_ARM);

	delay_ms(50);					/* device recovery time */

	NEREID_CTRL_REG = NC_HOST_STOP | NC_POWER | NC_INTENA;
	return USB_OK;
}

/*
 * A proper USB bus reset.  Only legal once host mode is on, i.e. after
 * usb_hw_init() has run.
 */
void usb_bus_reset(void)
{
	sl_wr(SL_CTRL1, C1_SE0);
	delay_ms(50);
	sl_wr(SL_CTRL1, C1_RUN);
	sl_wr(SL_PIDEP, PID_SOF);
	sl_wr(SL_DEVADDR, 0);
	sl_wr(SL_HOSTCTL, HC_ARM);
	delay_ms(50);
}

/*
 * Get to a configured device, trying progressively harder.
 */
int usb_start(void)
{
	int r = USB_ENODEV;
	int attempt;

	for (attempt = 0; attempt < 3; attempt++) {
		switch (attempt) {
		case 0:
			usb_hw_init(300);
			break;
		case 1:
			usb_bus_reset();	/* device may have been asleep */
			break;
		default:
			usb_hw_init(1000);	/* slow starting stick */
			break;
		}

		r = usb_enumerate();
		usb_try_isr[attempt] = sl_rd(SL_ISR);
		if (r == USB_OK)
			return USB_OK;
	}
	return r;
}

/* ------------------------------------------------------------------ *
 *  Enumeration
 * ------------------------------------------------------------------ */

#define DESC_DEVICE	1
#define DESC_CONFIG	2

static int get_descriptor(u8 type, u8 idx, void *buf, u16 len)
{
	return usb_control(0x80, 0x06, (u16)((type << 8) | idx), 0, len, buf);
}

/*
 * Walk the configuration descriptor looking for a Bulk Only mass storage
 * interface and its two bulk endpoints.
 */
static int parse_config(u8 *cfg, int total)
{
	int i = 0;
	int in_msc = 0;
	int found = 0;

	usb.ep_in = 0;
	usb.ep_out = 0;

	while (i + 1 < total) {
		int len = cfg[i];
		int type = cfg[i + 1];

		if (len < 2)
			break;

		if (type == 4 && i + 8 <= total) {		/* interface */
			u8 cls = cfg[i + 5];
			u8 sub = cfg[i + 6];
			u8 proto = cfg[i + 7];

			in_msc = 0;
			if (cls == 0x08 && proto == 0x50 && !found) {
				in_msc = 1;
				usb.iface = cfg[i + 2];
				usb.subclass = sub;
			}
		} else if (type == 5 && in_msc && i + 6 <= total) {	/* endpoint */
			u8 addr = cfg[i + 2];
			u8 attr = cfg[i + 3];
			u8 mps = cfg[i + 4];		/* low byte is enough */

			if ((attr & 0x03) == 0x02) {	/* bulk */
				if (mps == 0 || mps > SL_BUF_MAX)
					mps = SL_BUF_MAX;
				if (addr & 0x80) {
					usb.ep_in = addr & 0x0F;
					usb.in_size = mps;
				} else {
					usb.ep_out = addr & 0x0F;
					usb.out_size = mps;
				}
				if (usb.ep_in && usb.ep_out)
					found = 1;
			}
		}
		i += len;
	}
	return found ? USB_OK : USB_EUNSUP;
}

int usb_enumerate(void)
{
	int r, total;

	usb.addr = 0;
	usb.ep0size = 8;
	usb.tog_in = 0;
	usb.tog_out = 0;

	/* First eight bytes are enough to learn the endpoint 0 packet size.
	 * This is also where an empty port shows up: nothing answers. */
	r = get_descriptor(DESC_DEVICE, 0, descbuf, 8);
	if (r == USB_ETIMEOUT)
		return USB_ENODEV;
	if (r < 0)
		return r;
	if (r >= 8 && descbuf[7])
		usb.ep0size = descbuf[7] > SL_BUF_MAX ? SL_BUF_MAX : descbuf[7];

	r = usb_control(0x00, 0x05, 1, 0, 0, 0);	/* SET_ADDRESS 1 */
	if (r < 0)
		return r;
	usb.addr = 1;
	delay_ms(10);

	r = get_descriptor(DESC_DEVICE, 0, descbuf, 18);
	if (r < 0)
		return r;
	usb.vid = (u16)(descbuf[8] | (descbuf[9] << 8));
	usb.pid = (u16)(descbuf[10] | (descbuf[11] << 8));

	r = get_descriptor(DESC_CONFIG, 0, descbuf, 9);
	if (r < 9)
		return (r < 0) ? r : USB_EIO;
	total = descbuf[2] | (descbuf[3] << 8);
	if (total > (int)sizeof(descbuf))
		total = (int)sizeof(descbuf);

	r = get_descriptor(DESC_CONFIG, 0, descbuf, (u16)total);
	if (r < 0)
		return r;
	if (r < total)
		total = r;

	{
		u8 cfgval = descbuf[5];

		r = parse_config(descbuf, total);
		if (r != USB_OK)
			return r;

		r = usb_control(0x00, 0x09, cfgval, 0, 0, 0);	/* SET_CONFIG */
		if (r < 0)
			return r;
	}

	/* SET_CONFIGURATION resets every endpoint's toggle */
	usb.tog_in = 0;
	usb.tog_out = 0;
	delay_ms(50);
	return USB_OK;
}
