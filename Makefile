# uvmc2-menu: make uvm2 (the .um2), make test (host tests), make install CARD=/run/media/.../UVMC2
# Tool paths for this machine, if they are not on the PATH (see local.mk.example).
-include $(CURDIR)/local.mk

# The SDK is a submodule pinned to a release tag (git submodule update --init). Upgrade on purpose: check out the new
# tag in third_party/uvmc2-sdk, read its CHANGELOG.md, test on the console, commit the new pin.
UVMC2_SDK   ?= $(CURDIR)/third_party/uvmc2-sdk
# pico-sdk, the other submodule, at the tag the SDK asks for. Of its own submodules only TinyUSB is used (transfer.um2,
# which fetches it); WiFi, Bluetooth, lwIP and mbedTLS are not. uvm2.mk reads PICO_SDK_PATH.
export PICO_SDK_PATH := $(CURDIR)/third_party/pico-sdk
PICO_SDK_WANT = 2.2.0
UVM2_SDK    ?= $(UVMC2_SDK)/uvm2-sdk
RP2350_SDK  ?= $(UVMC2_SDK)/rp2350-sdk
VPY_C_SDK   ?= $(UVMC2_SDK)/vpy-c
PITREX_INC  ?= $(UVMC2_SDK)/pitrex-sim/include

# The release number, in the VERSION file only: the menu's title screen, the release archive and the changelog check read it.
MENU_VERSION := $(strip $(shell cat $(CURDIR)/VERSION))

MENU_DEFS = -DNM_MAX_RECS=1024 -DNM_POOL=114688 -DNM_KEY_LEN=96

UVM2_NAME    = uvmc2menu
UVM2_SRCS    = src/main.c src/menu_tree.c src/browse.c src/newlist.c src/um2.c src/launch.c src/font.c src/fonttext.c src/fwcat.c src/inflate.c src/hiscore.c src/cartserve.c src/bsave.c src/cart.c src/cartload.c src/cartstream.c $(VPY_C_SDK)/vpy.c
UVM2_CC      = arm-none-eabi-gcc
UVM2_CXX     = arm-none-eabi-gcc
UVM2_OBJCOPY = arm-none-eabi-objcopy
UVM2_CFLAGS  = -mthumb -mcpu=cortex-m33 -mfloat-abi=soft -ffreestanding -O2 -std=gnu11 \
               -ffunction-sections -fdata-sections -DVPY_RP2350 $(MENU_DEFS) -DMENU_VERSION=$(MENU_VERSION) -Wno-unused-function \
               -Isrc -Isrc/pitrex -I$(VPY_C_SDK)/include -I$(PITREX_INC) -I$(UVM2_SDK)
UVM2_PIO_STREAM ?= 1
UVM2_HZ ?= 50
# A full screen of text is ~670 strokes at ~14 commands each; the default 8192 holds ~575 and silently loses the rest.
UVM2_CMD_CAPACITY ?= 12288

# A fresh clone has the two folders empty: fetch the pinned commits, one level only (not the pico-sdk's own submodules).
ifneq ($(wildcard $(UVM2_SDK)/uvm2.mk),$(UVM2_SDK)/uvm2.mk)
$(info fetching the SDK submodules)
$(shell git -C "$(CURDIR)" submodule update --init third_party/uvmc2-sdk third_party/pico-sdk >&2)
endif
ifneq ($(wildcard $(PICO_SDK_PATH)/pico_sdk_init.cmake),$(PICO_SDK_PATH)/pico_sdk_init.cmake)
$(info fetching the SDK submodules)
$(shell git -C "$(CURDIR)" submodule update --init third_party/uvmc2-sdk third_party/pico-sdk >&2)
endif
ifeq ($(wildcard $(UVM2_SDK)/uvm2.mk),)
$(error no SDK in $(UVMC2_SDK): run git submodule update --init)
endif
ifeq ($(wildcard $(PICO_SDK_PATH)/pico_sdk_init.cmake),)
$(error no pico-sdk in $(PICO_SDK_PATH): run git submodule update --init)
endif
include $(UVM2_SDK)/uvm2.mk

# Stops a build against another pico-sdk than the one this was tested with.
.PHONY: sdk-check
sdk-check:
	@for t in arm-none-eabi-gcc cargo rustup cmake; do command -v $$t >/dev/null || { \
	  echo "$$t is not on the PATH: install it (README, Dependencies) or set its path in local.mk (see local.mk.example)"; exit 1; }; done
	@v=$$(sed -n 's/.*set(PICO_SDK_VERSION_\(MAJOR\|MINOR\|REVISION\) \([0-9]*\)).*/\2/p' "$(PICO_SDK_PATH)/pico_sdk_version.cmake" | paste -sd.); \
	  test "$$v" = "$(PICO_SDK_WANT)" || { echo "pico-sdk at $(PICO_SDK_PATH) is $$v, want $(PICO_SDK_WANT)"; exit 1; }
	@echo "uvmc2-sdk $$(git -C $(UVMC2_SDK) describe --tags --always --dirty), pico-sdk $(PICO_SDK_WANT)"
uvm2: sdk-check

HOSTCC ?= cc
test:
	mkdir -p build_host
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Wno-unused-function -g $(MENU_DEFS) -Isrc -Isrc/pitrex \
	  tests/test_menu.c src/menu_tree.c src/um2.c -o build_host/test_menu
	build_host/test_menu
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_font.c src/font.c -o build_host/test_font
	build_host/test_font
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_cart.c src/cart.c src/inflate.c src/sdsize.c -lz -o build_host/test_cart
	build_host/test_cart
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Wno-unused-function -g -Isrc -Isrc/pitrex tests/test_browse.c src/browse.c -o build_host/test_browse
	build_host/test_browse
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_hiscore.c src/hiscore.c -o build_host/test_hiscore
	build_host/test_hiscore
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Wno-unused-function -g -Isrc tests/test_newlist.c src/newlist.c src/browse.c src/menu_tree.c -Isrc/pitrex $(MENU_DEFS) -o build_host/test_newlist
	build_host/test_newlist
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_bsave.c src/bsave.c -o build_host/test_bsave
	build_host/test_bsave
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_fwsave.c src/fwcat.c src/bsave.c src/cart.c src/inflate.c -o build_host/test_fwsave
	build_host/test_fwsave

# Checks the scanner and inflate against real firmware images: make test-fw FW="a/flash.bin b/flash.bin"
test-fw:
	mkdir -p build_host
	$(HOSTCC) -std=gnu11 -O2 -Wall -Wextra -g -Isrc tests/test_fw.c src/fwcat.c src/bsave.c src/cart.c src/inflate.c -o build_host/test_fw
	build_host/test_fw $(FW)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -g -Isrc tests/test_fwsave.c src/fwcat.c src/bsave.c src/cart.c src/inflate.c -o build_host/test_fwsave
	build_host/test_fwsave $(FW)

# TRANSFER MODE's program, menu/transfer.um2 on the card (transfer.mk).
.PHONY: transfer
transfer:
	$(MAKE) -f transfer.mk uvm2

# Copy only. Of menu/ it replaces menu.default.ini and transfer.um2 and nothing else: your other files there are never
# touched.
# install-test: MENU.um2 only, start it from Browse SD card. Boot does not change.
# Both build first: CLion runs "make clean" whenever it reloads this Makefile, so the image may be gone.
install-test: uvm2 transfer
	test -n "$(CARD)" && test -d "$(CARD)"
	mkdir -p "$(CARD)/menu"
	cp build_uvm2/uvmc2menu.um2 "$(CARD)/MENU.um2"
	cp card/menu/menu.default.ini "$(CARD)/menu/"
	cp build_transfer/transfer.um2 "$(CARD)/menu/transfer.um2"
	sync

# install: also autostart.um2, so the menu starts at power-on. Rename that file on a PC to undo.
install: install-test
	cp build_uvm2/uvmc2menu.um2 "$(CARD)/autostart.um2"
	sync

# Runs the launch stub in an emulator (needs python packages unicorn, pyelftools).
PY ?= python3
stub-check: uvm2
	$(PY) -I tests/emulate_stub.py build_uvm2/pico/uvmc2menu.elf

# menu/catalog.ini: names, categories and developers from the firmware's list, for the games on this card.
card-ini:
	test -n "$(CARD)" && test -d "$(CARD)/menu"
	python3 -I tools/gen_card_ini.py "$(CARD)" > "$(CARD)/menu/catalog.ini.new"
	mv "$(CARD)/menu/catalog.ini.new" "$(CARD)/menu/catalog.ini"
	sync

preview:
	mkdir -p build_host
	$(HOSTCC) -std=gnu11 -O1 -Wno-unused-function $(MENU_DEFS) -Isrc -Isrc/pitrex tools/preview.c src/menu_tree.c src/fwcat.c src/cart.c src/inflate.c -o build_host/preview
	build_host/preview "$(CARD)" $(INIS)

# The release archive: dist/uvmc2-menu-<VERSION>.zip, with the files for the card under card/ and the documents beside it.
# Refuses to build if CHANGELOG.md has no heading for this VERSION.
dist: uvm2 transfer
	python3 -I tools/dist.py "$(MENU_VERSION)" build_uvm2/uvmc2menu.um2 build_transfer/transfer.um2 dist

clean:
	rm -rf build_uvm2 build_host
.PHONY: dist clean test install install-test preview stub-check card-ini test-fw
