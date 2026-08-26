# USBMSD.SYS / USBTEST.X - USB mass storage support for the Nereid card
#
# Build with xdev68k (https://github.com/yosshin4004/xdev68k) on any unix
# like host;  XDEV68K_DIR must point at the SDK.

ifndef XDEV68K_DIR
	$(error ERROR : XDEV68K_DIR is not defined.)
endif

.SUFFIXES:

CPU = 68000

ATOMIC = perl ${XDEV68K_DIR}/util/atomic.pl
CC = ${XDEV68K_DIR}/m68k-toolchain/bin/m68k-elf-gcc
GAS2HAS = perl ${XDEV68K_DIR}/util/x68k_gas2has.pl -cpu $(CPU) -inc doscall.inc -inc iocscall.inc
RUN68 = $(ATOMIC) ${XDEV68K_DIR}/run68/run68
HAS = $(RUN68) ${XDEV68K_DIR}/x68k_bin/HAS060.X
HLK = $(RUN68) ${XDEV68K_DIR}/x68k_bin/hlk301.x

# Finished binaries land in bin/, matching usbd/bin/ elsewhere in this tree.
BIN = bin
DRIVER = $(BIN)/USBMSD.SYS
TOOL = $(BIN)/USBTEST.X

INCLUDE_FLAGS = -I${XDEV68K_DIR}/include/xc -I${XDEV68K_DIR}/include/xdev68k

# -fno-zero-initialized-in-bss / -fno-common keep every variable in .data.
# A .SYS driver image is used exactly as it is on disk, so it must not rely
# on a BSS section being allocated and cleared for it - the link step below
# checks that none was created.
CFLAGS = -m$(CPU) -Os $(INCLUDE_FLAGS) \
	-fno-zero-initialized-in-bss -fno-common \
	-fomit-frame-pointer -fno-builtin \
	-Wall -Wno-builtin-declaration-mismatch \
	-fcall-used-d2 -fcall-used-a2 -fverbose-asm

# Shared core
CORE_C = usb.c scsi.c volume.c print.c
CORE_S = lowlevel.s

# driver.s has to come first: the device header must sit at offset 0 of the
# image.  zzend.s has to come last: it marks the end of the image.
DRIVER_OBJS = $(OBJ)/driver.o $(call objs,msd.c $(CORE_C)) $(call objs,$(CORE_S)) $(OBJ)/zzend.o
TOOL_OBJS = $(OBJ)/crt.o $(call objs,test.c $(CORE_C)) $(call objs,$(CORE_S))

LIBS = ${XDEV68K_DIR}/lib/m68k_elf/m$(CPU)/libgcc.a

OBJ = _build
objs = $(addprefix $(OBJ)/,$(addsuffix .o,$(basename $(1))))

all : $(DRIVER) $(TOOL)

clean :
	rm -f $(DRIVER) $(TOOL)
	rm -rf $(OBJ)
	@rmdir $(BIN) 2>/dev/null || true

# --- USBMSD.SYS ------------------------------------------------------------
$(DRIVER) : $(DRIVER_OBJS) $(LIBS) makefile
	@mkdir -p $(BIN)
	@rm -f $(OBJ)/_drv.tmp
	@first=1; for F in $(DRIVER_OBJS); do \
		if [ $$first = 1 ]; then echo "+$$F" >> $(OBJ)/_drv.tmp; first=0; \
		else echo $$F >> $(OBJ)/_drv.tmp; fi; \
	done
	@for F in $(LIBS); do \
		cp $$F $(OBJ)/`basename $$F`; \
		echo $(OBJ)/`basename $$F` >> $(OBJ)/_drv.tmp; \
	done
	$(HLK) -x -p $(OBJ)/map.txt -i $(OBJ)/_drv.tmp -o $(DRIVER)
	@text=`od -An -tu4 --endian=big -j12 -N4 $(DRIVER) | tr -d ' '`; \
	data=`od -An -tu4 --endian=big -j16 -N4 $(DRIVER) | tr -d ' '`; \
	bss=`od -An -tu4 --endian=big -j20 -N4 $(DRIVER) | tr -d ' '`; \
	echo "  $(DRIVER): text $$text, data $$data, bss $$bss"; \
	if [ "$$bss" != "0" ]; then \
		echo "ERROR: the driver image has a BSS section. Human68k does"; \
		echo "       not allocate one for a .SYS driver, so every"; \
		echo "       variable needs an initialiser."; \
		rm -f $(DRIVER); exit 1; \
	fi; \
	dend=`tr -d '\r' < $(OBJ)/map.txt | awk '/^data[ \t]*:/ {print $$5; exit}'`; \
	dsym=`tr -d '\r' < $(OBJ)/map.txt | \
		awk '/^_drv_data_end[ \t]*:/ && $$3 ~ /^[0-9a-f]+$$/ {print $$3; exit}'`; \
	if [ -z "$$dend" ] || [ -z "$$dsym" ] || \
	   [ $$((0x$$dend + 1)) -ne $$((0x$$dsym)) ]; then \
		echo "ERROR: _drv_data_end (0x$$dsym) is not the end of the"; \
		echo "       image (0x$$dend). Something got linked after"; \
		echo "       zzend.o, so the driver would report the wrong"; \
		echo "       resident size to Human68k."; \
		rm -f $(DRIVER); exit 1; \
	fi

# --- USBTEST.X -------------------------------------------------------------
$(TOOL) : $(TOOL_OBJS) $(LIBS) makefile
	@mkdir -p $(BIN)
	@rm -f $(OBJ)/_tool.tmp
	@for F in $(TOOL_OBJS); do echo $$F >> $(OBJ)/_tool.tmp; done
	@for F in $(LIBS); do \
		cp $$F $(OBJ)/`basename $$F`; \
		echo $(OBJ)/`basename $$F` >> $(OBJ)/_tool.tmp; \
	done
	$(HLK) -x -i $(OBJ)/_tool.tmp -o $(TOOL)

# --- rules -----------------------------------------------------------------
$(OBJ)/%.o : %.c usb.h scsi.h ddk.h volume.h print.h makefile
	@mkdir -p $(OBJ)
	$(CC) -S $(CFLAGS) -o $(OBJ)/$*.m68k-gas.s $<
	$(GAS2HAS) -i $(OBJ)/$*.m68k-gas.s -o $(OBJ)/$*.s
	$(HAS) -e -u -w0 $(INCLUDE_FLAGS) $(OBJ)/$*.s -o $(OBJ)/$*.o

$(OBJ)/%.o : %.s makefile
	@mkdir -p $(OBJ)
	$(HAS) -e -u -w0 $(INCLUDE_FLAGS) $*.s -o $(OBJ)/$*.o
