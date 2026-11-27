# JetSetWillyX: Jet Set Willy (48K) and Jet Set Willy 128 (v0.04 HL9) in one
# XBE for the original Xbox. With both tapes present it starts on a chooser
# built on the 48K title screen; with one, it runs that game.
#
#   export NXDK_DIR=/path/to/nxdk
#   make                      # -> bin/default.xbe and JetSetWillyX.iso
#
# JetSetWilly.tzx and JetSetWilly128.tzx (in this folder, the repository's
# root, or "Data files/") are copied next to default.xbe.

XBE_TITLE = JetSetWillyX
XBE_TITLEID = 4A535758    # "JSWX", shared by all three XBEs (cxbe writes FFFF0002; patched after linking)
GEN_XISO  = JetSetWillyX.iso
NXDK_SDL  = y

SRCS = $(CURDIR)/src/mainx.c \
       $(CURDIR)/src/jsw_x.c \
       $(CURDIR)/src/jsw_code_x.c \
       $(CURDIR)/src/jsw128_x.c \
       $(CURDIR)/src/jsw128_code_x.c \
       $(CURDIR)/src/z80c_glue_x.c \
       $(CURDIR)/src/tape_x.c \
       $(CURDIR)/src/zx_x.c \
       $(CURDIR)/src/zx_font_x.c \
       $(CURDIR)/src/ay_x.c

CFLAGS += -O2 -Wall -I$(CURDIR)/../src

NXDK_DIR ?= $(CURDIR)/../../nxdk

include $(NXDK_DIR)/Makefile

.PHONY: titleid gamedata
titleid: $(OUTPUT_DIR)/default.xbe
	@sh $(CURDIR)/../tools/xbe_titleid.sh $(OUTPUT_DIR)/default.xbe $(XBE_TITLEID)

gamedata: titleid
	@for name in JetSetWilly JetSetWilly128; do \
	  for f in "$(CURDIR)/$$name.tzx" "$(CURDIR)/../$$name.tzx" "$(CURDIR)/../Data files/$$name.tzx" \
	           "$(CURDIR)/$$name.tap" "$(CURDIR)/../$$name.tap" "$(CURDIR)/../Data files/$$name.tap"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] $$(basename "$$f")"; break; fi; done; done
	@for f in "$(CURDIR)/../48.rom" "$(CURDIR)/../Data files/48.rom"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] 48.rom"; break; fi; done

$(GEN_XISO): gamedata
all: gamedata
