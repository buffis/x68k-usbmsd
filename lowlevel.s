*******************************************************************************
*
*	lowlevel.s - helpers shared by USBMSD.SYS and USBTEST.X
*
*******************************************************************************

	.cpu	68000

SL811_DATA	equ	$ECE383		* SL811HS indexed data port

	.text
	.even

	.globl	_dos_print
	.globl	_sl_read_buf
	.globl	_sl_write_buf

*------------------------------------------------------------------------------
* void dos_print(const char *s)
*------------------------------------------------------------------------------
_dos_print:
	movem.l	d1-d7/a0-a6,-(sp)
	move.l	14*4+4(sp),-(sp)
	.dc.w	$FF09			* DOS _PRINT
	addq.l	#4,sp
	movem.l	(sp)+,d1-d7/a0-a6
	rts

*------------------------------------------------------------------------------
* void sl_read_buf(void *dst, int len)
* void sl_write_buf(const void *src, int len)
*
* Move a block of data to/from the SL811HS internal RAM.  The chip auto
* increments its internal address pointer on every access of the data port,
* so the address register must already point at the first byte.
*------------------------------------------------------------------------------
_sl_read_buf:
	move.l	4(sp),a1
	move.l	8(sp),d0
	subq.l	#1,d0
	bmi	slrb_end
	lea	SL811_DATA,a0
slrb_loop:
	move.b	(a0),(a1)+
	dbra	d0,slrb_loop
slrb_end:
	rts

_sl_write_buf:
	move.l	4(sp),a1
	move.l	8(sp),d0
	subq.l	#1,d0
	bmi	slwb_end
	lea	SL811_DATA,a0
slwb_loop:
	move.b	(a1)+,(a0)
	dbra	d0,slwb_loop
slwb_end:
	rts

	.even
