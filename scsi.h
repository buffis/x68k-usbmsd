/*
 *	scsi.h - USB Bulk Only Transport + the handful of SCSI commands
 *		 a memory stick needs
 */

#ifndef SCSI_H
#define SCSI_H

#include "usb.h"

#define SCSI_OK		0		/* command completed, status good */
#define SCSI_FAIL	1		/* device says "check condition" */
#define SCSI_PHASE	2		/* phase error, needs a reset */
#define SCSI_ERR	(-1)		/* transport broke down */

extern u8 scsi_sense[18];		/* sense data of the last failure */
extern u8 scsi_maxlun;
extern u32 scsi_residue;		/* bytes not transferred by the last command */

int	bot_reset(void);
int	bot_read_max_lun(void);

int	scsi_test_unit_ready(void);
int	scsi_request_sense(void);
int	scsi_inquiry(u8 *buf36);
int	scsi_read_capacity(u32 *last_lba, u32 *block_size);
int	scsi_start_unit(void);
int	scsi_read10(u32 lba, u16 count, void *buf);
int	scsi_write10(u32 lba, u16 count, const void *buf);

/* Wait for the medium to become usable.  Returns SCSI_OK when ready. */
int	scsi_wait_ready(int seconds);

#endif /* SCSI_H */
