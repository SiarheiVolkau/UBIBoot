/* Board-specific config for the RZX-27. */

#ifndef _CONFIG_H
#error Include "config.h" instead
#endif

#define CFG_CPU_SPEED   360000000
#define CFG_EXTAL       12000000

#define SYSPART_INIT    "/mininit-syspart"

/* serial parameters */
#define LOG_UART        0
#define LOG_BAUDRATE    57600

/* MMC parameters */
#define MMC_ID          1
#define MMC_1BIT        1
#define PAGE_SIZE       512 /* 512, 2048 or 4096 */
#define PAGE_PER_BLOCK  1
