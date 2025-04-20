MODEL = 77D
FW_VERSION = 110

TOP_DIR = ../..

AUTOEXEC_BASE = 0x800000
FIR_BASE = 0x800120

#CANON_FIR = CCF25110.FIR
FIRMWARE_ID = 0x80000408
MAIN_FIRMWARE_ADDR = 0xE0040000

# user_mem_start from cstart struct. Align to 0x10 up
#RESTARTSTART = 0xe0fa0
#ML_BOOT_OBJ = boot-d678.o

# copying ML to AllocMem region, see PTR_ALLOC_MEM_START
RESTARTSTART = 0x4d0000 # Aligning the end at 0x10000 allows
                        # more efficient packing for preceding
                        # MMU structs
ML_BOOT_OBJ = boot-d678-am.o

# Cortex A9, binaries loaded as Thumb
CFLAGS_PLATFORM = -mthumb -mlong-calls
PLATFORM_ARCH = armv7-a


ML_SRC_EXTRA_OBJS += $(BUILD_DIR)/function_overrides.o

include ../Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/cam_complete
