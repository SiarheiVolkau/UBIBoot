# Ritmix RZX-27 handheld
# - Ingenic JZ4725b
# - 32Mb SDRAM
# - internal 4Gb MMC0 (may be shipped with NAND)
# - 320x240 smart LCD (UC8230 based, may be shipped with another screens)
GC_FUNCTIONS = True
USE_SERIAL = True
BKLIGHT_ON = True
TRY_BOTH_MMCS = True
# USE_NAND = True
# USE_UBI = True
# STAGE1_ONLY = True

BOARD := rzx27

VARIANTS := UC8320_LCD
# actually jz4725b
JZ_VERSION = 4725

CFLAGS_all := -mips32 -DRZX27
CFLAGS_UC8320_LCD := -DUSE_SLCD_UC8230
