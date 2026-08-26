/*
 *	usb.h - SL811HS host controller / USB core for the Nereid card
 */

#ifndef USB_H
#define USB_H

typedef unsigned char	u8;
typedef unsigned short	u16;
typedef unsigned long	u32;

/* ------------------------------------------------------------------ *
 *  Nereid / SL811HS hardware
 * ------------------------------------------------------------------ */

#define SL811_ADDR_REG	(*(volatile u8 *)0xECE381)
#define SL811_DATA_REG	(*(volatile u8 *)0xECE383)
#define NEREID_CTRL_REG	(*(volatile u8 *)0xECE3F1)

/* Nereid USB control register */
#define NC_HOST_STOP	0x01		/* USB host held stopped */
#define NC_POWER	0x02		/* 5V bus power on */
#define NC_INTENA	0x04		/* let the card assert its interrupt */

/* SL811HS registers */
#define SL_HOSTCTL	0x00
#define  HC_ARM		 0x01
#define  HC_ENABLE	 0x02
#define  HC_OUT		 0x04		/* 0 = IN */
#define  HC_ISO		 0x10
#define  HC_AFTERSOF	 0x20
#define  HC_TOGGLE	 0x40		/* 0 = DATA0, 1 = DATA1 */
#define  HC_PREAMBLE	 0x80
#define SL_BUFADDR	0x01
#define SL_BUFLEN	0x02
#define SL_PKTSTAT	0x03		/* read */
#define  PS_ACK		 0x01
#define  PS_ERROR	 0x02
#define  PS_TIMEOUT	 0x04
#define  PS_SEQ		 0x08
#define  PS_SETUP	 0x10
#define  PS_OVERFLOW	 0x20
#define  PS_NAK		 0x40
#define  PS_STALL	 0x80
#define SL_PIDEP	0x03		/* write */
#define  PID_SETUP	 0xD0
#define  PID_IN		 0x90
#define  PID_OUT	 0x10
#define  PID_SOF	 0x50
#define SL_XFERCNT	0x04		/* read: bytes NOT transferred */
#define SL_DEVADDR	0x04		/* write */
#define SL_CTRL1	0x05
#define  C1_SOF_ENA	 0x01
#define  C1_SE0		 0x08		/* force SE0 = bus reset */
#define  C1_LOWSPEED	 0x20
#define  C1_SUSPEND	 0x40
/* What USBDRV.SYS writes to run a full speed bus.  Bit 2 is not documented
 * anywhere I could find, but it is what the working driver sets. */
#define  C1_RUN		 0x05
#define SL_IER		0x06
#define SL_ISR		0x0D
#define  IS_DONE_A	 0x01
#define  IS_SOF		 0x10
#define  IS_INSRMV	 0x20
#define  IS_DPLUS	 0x80		/* D+ high -> full speed device */
#define SL_HWREV	0x0E		/* read: revision in the top nibble */
#define SL_SOFLOW	0x0E		/* write */
#define SL_SOFTMR	0x0F		/* read: time left in this frame */
#define SL_CTRL2	0x0F		/* write */

/* The SL811 has 256 bytes of RAM; 0x10..0xff may be used for packet data.
 * We only ever have one transfer in flight, so a single buffer will do. */
#define SL_BUF_BASE	0x20
#define SL_BUF_MAX	64

/* ------------------------------------------------------------------ *
 *  Error codes (all negative)
 * ------------------------------------------------------------------ */

#define USB_OK		0
#define USB_ESTALL	(-1)		/* endpoint returned STALL */
#define USB_ETIMEOUT	(-2)		/* device did not answer */
#define USB_EIO		(-3)		/* bus / CRC / overflow error */
#define USB_ENODEV	(-4)		/* nothing plugged in */
#define USB_EUNSUP	(-5)		/* device is not what we want */

/* ------------------------------------------------------------------ *
 *  The one device we talk to
 * ------------------------------------------------------------------ */

struct usbdev {
	u8	addr;			/* assigned USB address */
	u8	ep0size;		/* endpoint 0 max packet size */
	u8	ep_in;			/* bulk IN endpoint number */
	u8	ep_out;			/* bulk OUT endpoint number */
	u8	in_size;		/* bulk IN max packet size */
	u8	out_size;		/* bulk OUT max packet size */
	u8	iface;			/* mass storage interface number */
	u8	subclass;		/* SCSI command set in use */
	u8	tog_in;			/* bulk data toggles, ours to keep */
	u8	tog_out;
	u16	vid;
	u16	pid;
};

extern struct usbdev usb;
extern u8 usb_init_isr;			/* SL811 status seen at power up */
extern u8 usb_try_isr[3];		/* and after each enumeration attempt */

/* ------------------------------------------------------------------ *
 *  API
 * ------------------------------------------------------------------ */

void	delay_ms(u32 ms);

int	usb_hw_init(u32 settle_ms);	/* power cycle the port, start frames */
void	usb_bus_reset(void);		/* SE0 pulse; needs host mode already on */
int	usb_enumerate(void);		/* address + configure whatever is there */
int	usb_start(void);		/* the two above, retried a few ways */

int	usb_chip_test(u8 *rev);		/* is the SL811 itself alive? */
u8	usb_reg_rd(u8 reg);
void	usb_reg_wr(u8 reg, u8 val);

int	usb_control(u8 type, u8 req, u16 value, u16 index, u16 len, void *data);
int	usb_bulk_in(void *buf, u32 len);
int	usb_bulk_out(const void *buf, u32 len);
int	usb_clear_halt(u8 ep_addr);

/* from driver.s */
void	dos_print(const char *s);
void	sl_read_buf(void *dst, int len);
void	sl_write_buf(const void *src, int len);

#endif /* USB_H */
