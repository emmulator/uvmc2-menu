# transfer.um2, the menu's TRANSFER MODE (transfer/): the SD card as a USB drive. A program of its own so that the
# menu keeps its RAM. Built by the main Makefile (make uvm2 / make transfer); its own build folder, build_transfer/.
-include $(CURDIR)/local.mk
UVMC2_SDK   ?= $(CURDIR)/third_party/uvmc2-sdk
export PICO_SDK_PATH := $(CURDIR)/third_party/pico-sdk
UVM2_SDK    ?= $(UVMC2_SDK)/uvm2-sdk
RP2350_SDK  ?= $(UVMC2_SDK)/rp2350-sdk
VPY_C_SDK   ?= $(UVMC2_SDK)/vpy-c
PITREX_INC  ?= $(UVMC2_SDK)/pitrex-sim/include

UVM2_NAME    = transfer
UVM2_BUILD   = build_transfer
# The menu's font and text helpers (src/font.c, src/fonttext.c), through VPy as in the menu, so the text matches.
UVM2_SRCS    = transfer/main.c transfer/usbdrive.c transfer/sdfast.c src/sdsize.c src/font.c src/fonttext.c \
               $(VPY_C_SDK)/vpy.c
UVM2_CC      = arm-none-eabi-gcc
UVM2_CXX     = arm-none-eabi-gcc
UVM2_OBJCOPY = arm-none-eabi-objcopy
# The SD bridge's settings (uvm2-sdk/sdbridge/build.sh): core 1 repeats the last picture, and RESET is only counted
# (main.c acts on it). TinyUSB's own sources look for tusb_config.h too (transfer/).
UVM2_CFLAGS  = -mthumb -mcpu=cortex-m33 -mfloat-abi=soft -ffreestanding -O2 -std=gnu11 \
               -ffunction-sections -fdata-sections -DVPY_RP2350 -DUVM2_REPLAY_LAST -DUVM2_RESET_WATCH_ONLY \
               -Wno-unused-function -Itransfer -Isrc -I$(VPY_C_SDK)/include -I$(PITREX_INC) -I$(UVM2_SDK)
UVM2_LDLIBS  = -ltinyusb_device
UVM2_TARGET_INCS = transfer
UVM2_HZ ?= 50

ifneq ($(wildcard $(PICO_SDK_PATH)/lib/tinyusb/src/tusb.h),$(PICO_SDK_PATH)/lib/tinyusb/src/tusb.h)
$(info fetching TinyUSB into the pico-sdk)
$(shell git -C "$(PICO_SDK_PATH)" submodule update --init lib/tinyusb >&2)
endif
include $(UVM2_SDK)/uvm2.mk
