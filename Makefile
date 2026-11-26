# Jet Set Willy for the original Xbox (nxdk + SDL2).
#
#   export NXDK_DIR=/path/to/nxdk
#   make                      # -> bin/default.xbe and JetSetWilly.iso
#
# Put JetSetWilly.tzx (the original tape; .tap or a 32K .bin also work) and
# optionally 48.rom in this folder (or in "Data files/") before running make
# and they are copied next to default.xbe and into the ISO.

XBE_TITLE = Jet\ Set\ Willy
XBE_TITLEID = 4A535700    # "JSW" 00 (cxbe writes FFFF0002; patched after linking)
GEN_XISO  = JetSetWilly.iso
NXDK_SDL  = y

SRCS = $(CURDIR)/src/main.c \
       $(CURDIR)/src/jsw.c \
       $(CURDIR)/src/jsw_code.c \
       $(CURDIR)/src/z80c_glue.c \
       $(CURDIR)/src/tape.c \
       $(CURDIR)/src/zx.c \
       $(CURDIR)/src/zx_font.c

CFLAGS += -O2 -Wall

NXDK_DIR ?= $(CURDIR)/../nxdk

include $(NXDK_DIR)/Makefile

# Copy the game data next to the XBE (before the ISO is built from bin/):
# the first tape/dump found, always named JetSetWilly.<ext>, plus 48.rom.
.PHONY: titleid gamedata
titleid: $(OUTPUT_DIR)/default.xbe
	@sh $(CURDIR)/tools/xbe_titleid.sh $(OUTPUT_DIR)/default.xbe $(XBE_TITLEID)

gamedata: titleid
	@for f in "$(CURDIR)/JetSetWilly.tzx" "$(CURDIR)/Data files/JetSetWilly.tzx" \
	          "$(CURDIR)/JetSetWilly.tap" "$(CURDIR)/Data files/JetSetWilly.tap" \
	          "$(CURDIR)/JetSetWilly.bin" "$(CURDIR)/Data files/JetSetWilly.bin"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] $$(basename "$$f")"; break; fi; done
	@for f in "$(CURDIR)/48.rom" "$(CURDIR)/Data files/48.rom"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] 48.rom"; break; fi; done

$(GEN_XISO): gamedata
all: gamedata
