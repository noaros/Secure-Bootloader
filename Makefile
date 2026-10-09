# Secure A/B bootloader demo for the NUCLEO-F429ZI.
#
#   make                         build bootloader + app v$(VERSION) for both slots
#   make flash                   factory-program: full erase, bootloader, app in slot A
#   make app VERSION=1.1.0 SECURITY_COUNTER=2
#   make update VERSION=1.1.0 SECURITY_COUNTER=2   send that build to the running board over UART
#   make update VERSION=1.2.0 SECURITY_COUNTER=2 DEMO_FAULT=1   ship a broken release -> rollback
#   make test                    host unit tests (no hardware needed)
#   make protect / unprotect     option-byte write protection (+ RDP=1 for readout protection)

CROSS            ?= arm-none-eabi-
CC               := $(CROSS)gcc
OBJCOPY          := $(CROSS)objcopy
SIZE             := $(CROSS)size
PYTHON           ?= python3
PROGRAMMER       ?= STM32_Programmer_CLI
PORT             ?= /dev/ttyACM0

VERSION          ?= 1.0.0
SECURITY_COUNTER ?= 1
DEMO_FAULT       ?= 0
KEY              ?= keys/signing_key.pem

BUILD    := build
APP_TAG  := app-v$(VERSION)-sc$(SECURITY_COUNTER)$(if $(filter 1,$(DEMO_FAULT)),-fault)
APP_A    := $(BUILD)/$(APP_TAG)-A
APP_B    := $(BUILD)/$(APP_TAG)-B
PUBKEY_C := $(BUILD)/pubkey.c

UECC_DEFS := -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
             -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
             -DuECC_SUPPORT_COMPRESSED_POINT=0
INCLUDES  := -Icommon -Ithird_party/micro-ecc

CFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -g3 -std=c11 \
           -Wall -Wextra -ffunction-sections -fdata-sections -ffreestanding \
           $(INCLUDES) $(UECC_DEFS)
LDFLAGS := -nostartfiles -Wl,--gc-sections -Llinker --specs=nano.specs \
           -Wl,--print-memory-usage

COMMON_SRC := common/startup.c common/board.c common/console.c common/flash_geom.c \
              common/flash_stm32.c common/sha256.c common/crc32.c common/image.c \
              common/bootstate.c common/update.c
UECC_OBJ   := $(BUILD)/uECC.o
COMMON_DEP := $(COMMON_SRC) $(UECC_OBJ) $(wildcard common/*.h) linker/sections.ld Makefile

.PHONY: all bootloader app keys flash update monitor protect unprotect test size clean
.SECONDARY:

all: bootloader app

bootloader: $(BUILD)/bootloader.bin
app: $(APP_A).signed.bin $(APP_B).signed.bin

# ---- keys ----------------------------------------------------------------

keys: $(KEY)

$(KEY):
	@echo "*** No signing key found - generating a new demo key in $(KEY)"
	@echo "*** (keep it private; boards only accept images signed with it)"
	$(PYTHON) tools/imgtool.py keygen $@

$(PUBKEY_C): $(KEY) tools/imgtool.py | $(BUILD)
	$(PYTHON) tools/imgtool.py pubkey-c $< $@

$(BUILD):
	mkdir -p $@

# Vendored library: compiled separately so its warnings don't drown ours.
$(UECC_OBJ): third_party/micro-ecc/uECC.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c $< -o $@

# ---- firmware ------------------------------------------------------------

$(BUILD)/bootloader.elf: bootloader/main.c $(COMMON_DEP) $(PUBKEY_C) linker/bootloader.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Tbootloader.ld -Wl,-Map=$(@:.elf=.map) \
		bootloader/main.c $(COMMON_SRC) $(UECC_OBJ) $(PUBKEY_C) -o $@

$(APP_A).elf: app/main.c $(COMMON_DEP) $(PUBKEY_C) linker/app_slot_a.ld
	$(CC) $(CFLAGS) -DAPP_SLOT=0 -DDEMO_FAULT=$(DEMO_FAULT) $(LDFLAGS) -Tapp_slot_a.ld \
		app/main.c $(COMMON_SRC) $(UECC_OBJ) $(PUBKEY_C) -o $@

$(APP_B).elf: app/main.c $(COMMON_DEP) $(PUBKEY_C) linker/app_slot_b.ld
	$(CC) $(CFLAGS) -DAPP_SLOT=1 -DDEMO_FAULT=$(DEMO_FAULT) $(LDFLAGS) -Tapp_slot_b.ld \
		app/main.c $(COMMON_SRC) $(UECC_OBJ) $(PUBKEY_C) -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

$(APP_A).signed.bin: $(APP_A).bin $(KEY)
	$(PYTHON) tools/imgtool.py sign --key $(KEY) --load-addr 0x08020000 \
		--version $(VERSION) --security-counter $(SECURITY_COUNTER) $< $@

$(APP_B).signed.bin: $(APP_B).bin $(KEY)
	$(PYTHON) tools/imgtool.py sign --key $(KEY) --load-addr 0x08120000 \
		--version $(VERSION) --security-counter $(SECURITY_COUNTER) $< $@

size: $(BUILD)/bootloader.elf $(APP_A).elf
	$(SIZE) $^

# ---- board ---------------------------------------------------------------

flash: $(BUILD)/bootloader.bin $(APP_A).signed.bin
	$(PROGRAMMER) -c port=SWD mode=UR -e all \
		-w $(BUILD)/bootloader.bin 0x08000000 -v \
		-w $(APP_A).signed.bin 0x08020000 -v -rst

update: $(APP_A).signed.bin $(APP_B).signed.bin
	$(PYTHON) tools/update.py --port $(PORT) $^

monitor:
	$(PYTHON) tools/update.py --port $(PORT)

# Write-protect the bootloader sectors (0-1). RDP=1 additionally enables
# readout protection; undoing that mass-erases the chip. Never use level 2
# on a dev board: it is permanent.
# CubeProgrammer exposes one nWRPi bit per sector (0 = protected) and only
# warns about invalid values, so keep these as single bits. SPRMOD=0 makes the
# bits mean write protection rather than PCROP.
protect:
	$(PROGRAMMER) -c port=SWD mode=UR -ob SPRMOD=0 nWRP0=0 nWRP1=0 $(if $(filter 1,$(RDP)),RDP=0xBB)

unprotect:
	$(PROGRAMMER) -c port=SWD mode=UR -ob RDP=0xAA SPRMOD=0 nWRP0=1 nWRP1=1

# ---- host tests ----------------------------------------------------------

TEST_DIR := $(BUILD)/test
HOST_CC  ?= cc
TEST_SRC := test/host_test.c test/fake_flash.c common/console.c common/flash_geom.c \
            common/sha256.c common/crc32.c common/image.c common/bootstate.c \
            third_party/micro-ecc/uECC.c

test:
	mkdir -p $(TEST_DIR)
	$(PYTHON) test/gen_images.py $(TEST_DIR)
	$(HOST_CC) -O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined $(INCLUDES) \
		-Itest $(UECC_DEFS) $(TEST_SRC) $(TEST_DIR)/pubkey.c -o $(TEST_DIR)/host_test
	$(TEST_DIR)/host_test $(TEST_DIR)

clean:
	rm -rf $(BUILD)
