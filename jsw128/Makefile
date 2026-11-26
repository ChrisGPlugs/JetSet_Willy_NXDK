# Jet Set Willy 128 (John Elliott, v0.04 HL9) for the original Xbox.
#
#   export NXDK_DIR=/path/to/nxdk
#   make                      # -> bin/default.xbe and JetSetWilly128.iso
#
# Put JetSetWilly128.tzx (the HL9 tape) in this folder, in the repository's
# root, or in "Data files/", and it is copied next to default.xbe.

XBE_TITLE = Jet\ Set\ Willy\ 128
XBE_TITLEID = 4A535701    # "JSW" 01 (cxbe writes FFFF0002; patched after linking)
GEN_XISO  = JetSetWilly128.iso
NXDK_SDL  = y

SRCS = $(CURDIR)/src/main128.c \
       $(CURDIR)/src/jsw128_128.c \
       $(CURDIR)/src/jsw128_code_128.c \
       $(CURDIR)/src/z80c_glue_128.c \
       $(CURDIR)/src/tape_128.c \
       $(CURDIR)/src/zx_128.c \
       $(CURDIR)/src/zx_font_128.c \
       $(CURDIR)/src/ay_128.c

CFLAGS += -O2 -Wall -I$(CURDIR)/../src

NXDK_DIR ?= $(CURDIR)/../../nxdk

include $(NXDK_DIR)/Makefile

.PHONY: titleid gamedata
titleid: $(OUTPUT_DIR)/default.xbe
	@sh $(CURDIR)/../tools/xbe_titleid.sh $(OUTPUT_DIR)/default.xbe $(XBE_TITLEID)

gamedata: titleid
	@for f in "$(CURDIR)/JetSetWilly128.tzx" "$(CURDIR)/../JetSetWilly128.tzx" "$(CURDIR)/../Data files/JetSetWilly128.tzx" \
	          "$(CURDIR)/JetSetWilly128.tap" "$(CURDIR)/../JetSetWilly128.tap" "$(CURDIR)/../Data files/JetSetWilly128.tap"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] $$(basename "$$f")"; break; fi; done
	@for f in "$(CURDIR)/../48.rom" "$(CURDIR)/../Data files/48.rom"; do \
	    if [ -f "$$f" ]; then cp "$$f" $(OUTPUT_DIR)/; echo "[ COPY     ] 48.rom"; break; fi; done

$(GEN_XISO): gamedata
all: gamedata
