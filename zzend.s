*******************************************************************************
*
*	End of image markers.
*
*	This object file must be the LAST one in the link list.  The labels
*	below then sit at the end of the text and data sections, which is what
*	the init routine reports to Human68k as the "end of resident code"
*	address.
*
*	The build refuses to produce a driver that has a BSS section, so the
*	end of .data really is the end of the whole image.
*
*******************************************************************************

	.cpu	68000

	.globl	_drv_text_end
	.globl	_drv_data_end

	.text
	.even
_drv_text_end:

	.data
	.even
_drv_data_end:
