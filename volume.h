/*
 *	volume.h - bringing the stick up and working out what is on it
 */

#ifndef VOLUME_H
#define VOLUME_H

#include "usb.h"
#include "ddk.h"

#define MAX_UNITS	4
#define SECT_SIZE	512

struct unit {
	u32		start;		/* first LBA of this partition */
	u32		nsect;		/* size in sectors */
	struct bpb	bpb;		/* what Human68k gets to see */
};

extern struct unit vol_units[MAX_UNITS];
extern int	vol_nunits;
extern u32	vol_lastlba;		/* last addressable LBA of the medium */
extern u8	vol_secbuf[SECT_SIZE];	/* scratch sector */
extern int	vol_verbose;

/* Power up the port, enumerate, wait for the medium, read the capacity.
 * Prints what it finds / what went wrong.  Returns 0 on success. */
int	vol_probe(void);

/* Fill in vol_units[] from the partition table (or the whole medium). */
void	vol_scan(void);

/* Read or write whole 512 byte sectors, with retries.  0 on success. */
int	vol_io(int is_write, u32 lba, u16 count, void *buf);

#endif /* VOLUME_H */
