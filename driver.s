*******************************************************************************
*
*	USBMSD.SYS - USB Mass Storage (memory stick) driver for Nereid
*
*	Human68k device driver header and entry points.  The header has to be
*	at the very start of the image, so this object file must be the FIRST
*	one in the link list.
*
*******************************************************************************

	.cpu	68000

	.text
	.even

	.globl	_drv_header

*------------------------------------------------------------------------------
* Human68k device driver header
*
* +0  .l  pointer to next device header ( -1 = last )
* +4  .w  device attribute word ( bit15 = 0 -> block device )
* +6  .l  strategy entry
* +10 .l  interrupt entry
* +14 .b  number of units (block device)
* +15     7 character device name
*------------------------------------------------------------------------------
_drv_header:
	.dc.l	-1
	.dc.w	$0000			* block device
	.dc.l	strategy
	.dc.l	interrupt
	.dc.b	1,'USBMSD '

*------------------------------------------------------------------------------
* Saved request packet pointer (handed over in a5 by the strategy call)
*------------------------------------------------------------------------------
req_ptr:
	.dc.l	0

*------------------------------------------------------------------------------
* Strategy : just remember the request packet
*------------------------------------------------------------------------------
strategy:
	move.l	a5,req_ptr
	rts

*------------------------------------------------------------------------------
* Interrupt : do the actual work.
*
* Human68k calls this in supervisor mode and expects every register to be
* preserved.  The 16 bit return status goes to +3 of the request packet,
* which is an odd address, so it has to be stored as two bytes.
*------------------------------------------------------------------------------
interrupt:
	movem.l	d0-d7/a0-a6,-(sp)
	move.l	req_ptr,-(sp)
	jsr	_msd_dispatch		* d0.w = status word
	addq.l	#4,sp
	move.l	req_ptr,a5
	move.w	d0,d1
	lsr.w	#8,d1
	move.b	d1,3(a5)		* status (high byte)
	move.b	d0,4(a5)		* status (low byte)
	movem.l	(sp)+,d0-d7/a0-a6
	rts

	.even
