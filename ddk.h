/*
 *	ddk.h - the bits of the Human68k device driver interface this
 *		driver needs.
 *
 *	Request packet layout (block device):
 *
 *	  +0  .b  packet length
 *	  +1  .b  unit number
 *	  +2  .b  command code
 *	  +3  .w  return status
 *	  +5      8 reserved bytes
 *	  +13 .b  number of units (init, out) / media byte / drive status
 *	  +14 .l  end address (init, out) / transfer address
 *	  +14 .b  media change code (media check, out)
 *	  +18 .l  BPB pointer array (init, out) / sector count
 *	  +18 .l  argument string (init, in)
 *	  +22 .l  start sector / drive number (init, in, byte)
 */

#ifndef DDK_H
#define DDK_H

/* command codes */
#define C_INIT		0x00
#define C_MEDIACHK	0x01
#define C_BLDBPB	0x02
#define C_IOCTLIN	0x03
#define C_INPUT		0x04
#define C_DRVCTL	0x05
#define C_OUTPUT	0x08
#define C_OUTVFY	0x09
#define C_IOCTLOUT	0x0C

/* status word, high byte */
#define S_ABORT		0x1000
#define S_RETRY		0x2000
#define S_IGNORE	0x4000

/* status word, low byte */
#define E_OK		0x0000
#define E_UNIT		0x0001
#define E_NOTRDY	0x0002
#define E_CMD		0x0003
#define E_CRC		0x0004
#define E_LENGTH	0x0005
#define E_SEEK		0x0006
#define E_MEDIA		0x0007
#define E_NOTFND	0x0008
#define E_WRITE		0x000A
#define E_READ		0x000B
#define E_FAILURE	0x000C
#define E_WRPRT		0x000D
#define E_DISK		0x000E

/* media check reply */
#define M_CHANGED	0xFF
#define M_DONT_KNOW	0x00
#define M_NOT_CHANGED	0x01

/* drive status bits (C_DRVCTL) */
#define DS_LED_ON		0x80
#define DS_EJECT_UNAVAILABLE	0x40
#define DS_WRITE_PROTECTED	0x08
#define DS_DRIVE_NOT_READY	0x04
#define DS_MEDIA_INSERTED	0x02

/*
 * BIOS parameter block as Human68k wants it.  Note that this is not quite
 * the MS-DOS layout: the FAT count comes before the reserved sector count,
 * and there is a 32 bit size field used when the 16 bit one is zero.
 */
struct bpb {
	unsigned short	nbyte;		/* bytes per sector */
	unsigned char	nsector;	/* sectors per cluster */
	unsigned char	nfat;		/* number of FATs */
	unsigned short	nreserved;	/* reserved sectors before the FAT */
	unsigned short	ndirent;	/* root directory entries */
	unsigned short	nsize;		/* total sectors, 0 -> see huge */
	unsigned char	mdesc;		/* media descriptor byte */
	unsigned char	nfsect;		/* sectors per FAT */
	unsigned long	huge;		/* total sectors, 32 bit */
};

#endif /* DDK_H */
