*******************************************************************************
*
*	crt.s - tiny start up code for USBTEST.X
*
*	The SL811 registers and the MFP live in I/O space, so the whole
*	program runs in supervisor mode.
*
*******************************************************************************

	.cpu	68000

	.text
	.even

	.globl	__main

__main:
	move.l	a2,cmdline		* a2 = command line (length byte first)

	clr.l	-(sp)
	.dc.w	$FF20			* DOS _SUPER(0)
	addq.l	#4,sp
	move.l	d0,ssp_save

	move.l	cmdline,a2
	pea	1(a2)
	jsr	_test_main
	addq.l	#4,sp
	move.w	d0,exit_code

	move.l	ssp_save,d0		* back to user mode, unless we were
	beq	no_user			* called in supervisor mode already
	addq.l	#1,d0
	beq	no_user
	move.l	ssp_save,-(sp)
	.dc.w	$FF20			* DOS _SUPER(ssp)
	addq.l	#4,sp
no_user:
	move.w	exit_code,-(sp)
	.dc.w	$FF4C			* DOS _EXIT2

cmdline:	.dc.l	0
ssp_save:	.dc.l	0
exit_code:	.dc.w	0

	.even
	.end	__main
