/*
 * board.c
 *
 * Board init routines.
 *
 * Copyright (C) 2006 Ingenic Semiconductor Inc.
 *
 */

#include <stdint.h>

#include "config.h"

#include "board.h"
#include "serial.h"
#include "utils.h"

#include "jz.h"
#include "jz4740-cpm.h"
#include "jz4740-emc.h"
#include "jz4740-gpio.h"
#include "jz4740-lcd.h"

#define CDIV 1
#define HDIV 3
#define PDIV 3
#define MDIV 3
#define LDIV 3

/* PLL output clock = EXTAL * NF / (NR * NO)
 *
 * NF = FD + 2, NR = RD + 2
 * NO = 1 (if OD = 0), NO = 2 (if OD = 1 or 2), NO = 4 (if OD = 3)
 */
static void pll_init(void)
{
	register unsigned int cfcr, plcr1, pllout2;
	static const uint8_t n2FR[33] = {
		0, 0, 1, 2, 3, 0, 4, 0, 5, 0, 0, 0, 6, 0, 0, 0,
		7, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0,
		9
	};

	cfcr = CPM_CPCCR_CLKOEN |
		CPM_CPCCR_PCS |
		((unsigned int) n2FR[CDIV] << CPM_CPCCR_CDIV_BIT) |
		((unsigned int) n2FR[HDIV] << CPM_CPCCR_HDIV_BIT) |
		((unsigned int) n2FR[PDIV] << CPM_CPCCR_PDIV_BIT) |
		((unsigned int) n2FR[MDIV] << CPM_CPCCR_MDIV_BIT) |
		((unsigned int) n2FR[LDIV] << CPM_CPCCR_LDIV_BIT);

	pllout2 = (cfcr & CPM_CPCCR_PCS) ? CFG_CPU_SPEED : (CFG_CPU_SPEED / 2);

	/* Init USB Host clock, pllout2 must be n*48MHz */
	REG_CPM_UHCCDR = pllout2 / 48000000 - 1;
#ifdef USE_SLCD_UC8230
	/* Init LCD clock to 20MHz (10MHz effective dot clock) */
	REG_CPM_LPCDR = pllout2 / 20000000 - 1;
#endif

#define NF (CFG_CPU_SPEED * 2 / CFG_EXTAL)
	plcr1 = ((NF - 2) << CPM_CPPCR_PLLM_BIT) | /* FD */
		(0 << CPM_CPPCR_PLLN_BIT) |	/* RD=0, NR=2 */
		(0 << CPM_CPPCR_PLLOD_BIT) |    /* OD=0, NO=1 */
		(0x20 << CPM_CPPCR_PLLST_BIT) | /* PLL stable time */
		CPM_CPPCR_PLLEN;                /* enable PLL */          

	/* init PLL */
	REG_CPM_CPCCR = cfcr;
	REG_CPM_CPPCR = plcr1;

	__cpm_enable_pll_change();
	while (!__cpm_pll_is_on());
}

/*
 * Failsafe SDRAM configuration values
 *
 * If you want to live on the edge, the Dingoo Hynix HY57V281620FTP-6
 * chips should work with these accoring to the datasheet:
 *
 *   TRAS 42
 *   RCD  18
 *   TPC  18
 *
 */

#define SDRAM_CASL		3	/* CAS latency: 2 or 3 */
#define SDRAM_TRAS	42		/* RAS# Active Time (ns) */
#define SDRAM_RCD	18		/* RAS# to CAS# Delay (ns) */
#define SDRAM_TPC	18		/* RAS# Precharge Time (ns) */
#define SDRAM_TREF	15625		/* Refresh period (ns) */
#define SDRAM_TRWL		7	/* Write Latency Time (ns) */
#define SDRAM_BW16		1
#define SDRAM_BANK40		0
#define SDRAM_BANK4		1
#define SDRAM_ROW0		11
#define SDRAM_ROW		13
#define SDRAM_COL0		9
#define SDRAM_COL		9

static void sdram_init(void)
{
	unsigned int dmcr0, dmcr, sdmode, tmp;

	static const unsigned int cas_latency_sdmr[2] = {
		EMC_SDMR_CAS_2,
		EMC_SDMR_CAS_3,
	};

	static const unsigned int cas_latency_dmcr[2] = {
		1 << EMC_DMCR_TCL_BIT,	/* CAS latency is 2 */
		2 << EMC_DMCR_TCL_BIT	/* CAS latency is 3 */
	};

	REG_EMC_BCR = 0;	/* Disable bus release */
	REG_EMC_RTCSR = 0;	/* Disable clock for counting */

	/* Fault DMCR value for mode register setting*/
	dmcr0 = ((SDRAM_ROW0-11)<<EMC_DMCR_RA_BIT) |
		((SDRAM_COL0-8)<<EMC_DMCR_CA_BIT) |
		(SDRAM_BANK40<<EMC_DMCR_BA_BIT) |
		(SDRAM_BW16<<EMC_DMCR_BW_BIT) |
		EMC_DMCR_EPIN |
		cas_latency_dmcr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* Basic DMCR value */
	dmcr = ((SDRAM_ROW-11)<<EMC_DMCR_RA_BIT) |
		((SDRAM_COL-8)<<EMC_DMCR_CA_BIT) |
		(SDRAM_BANK4<<EMC_DMCR_BA_BIT) |
		(SDRAM_BW16<<EMC_DMCR_BW_BIT) |
		EMC_DMCR_EPIN |
		cas_latency_dmcr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* SDRAM timimg */
#define NS (1000000000 / (CFG_CPU_SPEED * CDIV / MDIV))
	tmp = SDRAM_TRAS/NS;
	if (tmp < 4) tmp = 4;
	if (tmp > 11) tmp = 11;
	dmcr |= ((tmp-4) << EMC_DMCR_TRAS_BIT);
	tmp = SDRAM_RCD/NS;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_RCD_BIT);
	tmp = SDRAM_TPC/NS;
	if (tmp > 7) tmp = 7;
	dmcr |= (tmp << EMC_DMCR_TPC_BIT);
	tmp = SDRAM_TRWL/NS;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_TRWL_BIT);
	tmp = (SDRAM_TRAS + SDRAM_TPC)/NS;
	if (tmp > 14) tmp = 14;
	dmcr |= (((tmp + 1) >> 1) << EMC_DMCR_TRC_BIT);

	/* SDRAM mode value */
	sdmode = EMC_SDMR_BT_SEQ | 
		 EMC_SDMR_OM_NORMAL |
		 EMC_SDMR_BL_4 | 
		 cas_latency_sdmr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* Stage 1. Precharge all banks by writing SDMR with DMCR.MRSET=0 */
	REG_EMC_DMCR = dmcr;
	REG8(EMC_SDMR0|sdmode) = 0;

	/* Wait for precharge, > 200us */
	udelay(1000);

	/* Stage 2. Enable auto-refresh */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH;

	tmp = SDRAM_TREF/NS;
	tmp = tmp/64 + 1;
	if (tmp > 0xff) tmp = 0xff;
	REG_EMC_RTCOR = tmp;
	REG_EMC_RTCNT = 0;
	REG_EMC_RTCSR = EMC_RTCSR_CKS_64;	/* Divisor is 64, CKO/64 */

	/* Wait for number of auto-refresh cycles */
	udelay(1000);

 	/* Stage 3. Mode Register Set */
	REG_EMC_DMCR = dmcr0 | EMC_DMCR_RFSH | EMC_DMCR_MRSET;
	REG8(EMC_SDMR0|sdmode) = 0;

        /* Set back to basic DMCR value */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH | EMC_DMCR_MRSET;

	/* everything is ok now */
}

int alt_key_pressed(void)
{
	return 0; /* TODO */
}

int alt2_key_pressed(void)
{
	return 0; /* TODO */
}

int alt3_key_pressed(void)
{
	return 0; /* TODO */
}

#ifdef BKLIGHT_ON
void light(int set)
{
	if (set)
		__gpio_set_pin(GPIOC, 15);
	else
		__gpio_clear_pin(GPIOC, 15);
}
#endif

unsigned int get_memory_size(void)
{
	return 1 << (SDRAM_ROW + SDRAM_COL + (2 - SDRAM_BW16) +
				(2 - SDRAM_BANK4) + 1);
}

/* preparing LCD for use in Linux */
#ifdef USE_SLCD_UC8230

#define CMD_END   0xFFFF
#define CMD_DELAY 0xFFFE
static uint16_t UC8230_regValues[] = {
	//After pin Reset wait at least 100ms
	CMD_DELAY, 100, //at least 100ms
	0x0046, 0x0002, //MTP Disable
	0x0010, 0x10b0, //SAP=1, BT=0, APE=1, AP=3
	0x0011, 0x0227, //DC1=2, DC0=2, VC=7
	0x0012, 0x80ff, //P5VMD=1, PON=7, VRH=15
	0x0013, 0x9F31, //VDV=31, VCM=49
	CMD_DELAY, 10, //at least 10ms
	0x0003, 0x1038, //set GRAM writing direction & BGR=1
	0x0060, 0xa700, //GS; gate scan: start position & drive line Q'ty - mirrors across vertical center line
	0x0061, 0x0001, //REV, NDL, VLE - mandatory to have normal colors
	0x0020, 0x0000, //GRAM horizontal address
	0x0021, 0x0000, //GRAM vertical address
	0x0050, 0x0000, // write window y-start 0
	0x0051, 240 -1, // write window y-end 240-1 (whole y size)
	0x0052, 0x0000, // write window x-start 0
	0x0053, 320 -1, // write window x-end 320-1 (whole x size)
	0x0080, 0x0000,
	0x0081, 0x0000,
	0x0082, 0x0000,
	0x0083, 0x0000,
	0x0084, 0x0000,
	0x0085, 0x0000,
	0x0092, 0x0200,
	0x0093, 0x0303,
	0x0090, 0x0010, //set clocks/Line
	0x000C, 0x0000,
	0x0000, 0x0001,
	CMD_DELAY, 200, // Delay 200 ms
	0x0007, 0x0173, //Display on setting
	CMD_END,
};

static void write_slcd_ir(uint16_t reg)
{
	// set data lanes as gpio output
	__gpio_as_output_mask(GPIOD, 0xff);
	// set RS pin into `cmd` (low)
	__gpio_clear_pin(GPIOD, 19);
	// set all data lanes as 0x00
	__gpio_clear_pin_mask(GPIOD, 0xFF);
	__gpio_set_pin_mask(GPIOD, reg >> 8);
	// issue #WR twice
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);

	__gpio_clear_pin_mask(GPIOD, 0xFF);
	__gpio_set_pin_mask(GPIOD, reg & 0xFF);
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);

	udelay(10);
	// set RS pin into `data` (high)
	__gpio_set_pin(GPIOD, 19);
}

static void write_slcd_reg(uint16_t reg, uint16_t val)
{
	write_slcd_ir(reg);

	__gpio_clear_pin_mask(GPIOD, 0xFF);
	__gpio_set_pin_mask(GPIOD, val >> 8);
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);

	__gpio_clear_pin_mask(GPIOD, 0xFF);
	__gpio_set_pin_mask(GPIOD, val & 0xFF);
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);
	udelay(10);
}

static void read_slcd_chip_id(void)
{
	uint16_t chip_id = 0;

	// set data lanes as gpio output
	__gpio_as_output_mask(GPIOD, 0xff);
	// set RS pin into `cmd` (low)
	__gpio_clear_pin(GPIOD, 19);
	// set all data lanes as 0x00
	__gpio_clear_pin_mask(GPIOD, 0xFF);
	// issue #WR twice
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);
	udelay(10);
	__gpio_clear_pin(GPIOD, 20);
	udelay(10);
	__gpio_set_pin(GPIOD, 20);
	udelay(10);

	// set data lanes as gpio input
	__gpio_as_input_mask(GPIOD, 0xff);
	// set RS pin into `data` (high)
	__gpio_set_pin(GPIOD, 19);
	// issue #RD
	udelay(10);
	__gpio_clear_pin(GPIOD, 18);
	udelay(10);
	// read the byte
	chip_id = (REG32(GPIO_PXPIN(GPIOD)) & (0xff)) << 8;
	// release #RD
	__gpio_set_pin(GPIOD, 18);
	// issue #RD
	udelay(10);
	__gpio_clear_pin(GPIOD, 18);
	udelay(10);
	// read the byte
	chip_id |= (REG32(GPIO_PXPIN(GPIOD)) & (0xff));
	// release #RD
	__gpio_set_pin(GPIOD, 18);
	// set data lanes as gpio output
	__gpio_as_output_mask(GPIOD, 0xff);

	SERIAL_PUTS_ARGH("LCD controller ID: ", chip_id, "\n");
}

static void clear_slcd_screen(void)
{
	int i;

	/* put the 0x00 to all pixels  */
	__gpio_clear_pin_mask(GPIOD, 0xFF);

	for (i = 0; i < 240 * 320 * 2; i++) {
		__gpio_clear_pin(GPIOD, 20);
		__gpio_set_pin(GPIOD, 20);
	}

	// restore WDR position
	__gpio_clear_pin(GPIOD, 19);
	udelay(10000);
	__gpio_set_pin(GPIOD, 19);
}

// Emulating an i80 8-bit bus via GPIO
// as we need read chip id back.
// Ingenic SLCD controller doesn't support reading.
static void init_smart_lcd_8230(void)
{
	const uint16_t *pos = UC8230_regValues;

	/* #RST pin PD21 */
	__gpio_set_pin(GPIOD, 21);
	__gpio_as_output(GPIOD, 21);

	/* CS pin PC13 */
	__gpio_clear_pin(GPIOC, 13);
	__gpio_as_output(GPIOC, 13);
	/* RS pin */
	__gpio_set_pin(GPIOD, 19); // `set` for `data` op
	__gpio_as_output(GPIOD, 19);
	/* #WR pin */
	__gpio_set_pin(GPIOD, 20);
	__gpio_as_output(GPIOD, 20);
	/* #RD pin */
	__gpio_set_pin(GPIOD, 18);
	__gpio_as_output(GPIOD, 18);

	/* DATA 7..0 */
	__gpio_as_output_mask(GPIOD, 0xff);

	// issue reset
	__gpio_clear_pin(GPIOD, 21);
	udelay(10000);
	__gpio_set_pin(GPIOD, 21);
	read_slcd_chip_id();

	while (pos[0] != CMD_END) {
		if (pos[0] == CMD_DELAY) {
			udelay(pos[1] * 1000);
		} else {
			write_slcd_reg(pos[0], pos[1]);
		}
		pos += 2;
	}

	write_slcd_ir(0x22); // set pointer to WDR
	clear_slcd_screen();

	__gpio_set_pin(GPIOC, 13);
}
#endif

void board_init(void)
{
#ifdef USE_NAND
	__gpio_as_func_mask(GPIOC, 0x36300300, 0);
	__gpio_as_input(GPIOC, 27);
	__gpio_disable_pull(GPIOC, 27);
#else
	/* MSC0 pins */
	__gpio_as_func_mask(GPIOC, 0x30400300, 1);
	__gpio_as_func_mask(GPIOC, 0x08000000, 0);
#endif

	/* SDRAM pins */
	__gpio_as_func_mask(0, 0xffff, 0);
	__gpio_as_func_mask(1, 0x033fffff, 0);

	/* MSC1 pins */
	__gpio_as_func_mask(GPIOD, 0x1c000000, 0);

#ifdef USE_SERIAL
	__gpio_as_func(2, 12, 1); /* UART_TX */

	/* Start UART clock */
	REG_CPM_CLKGR &= ~BIT(0);

	serial_init();
#endif
#ifdef USE_SLCD_UC8230
	/* ungate the LCD/SLCD clock */
	REG_CPM_CLKGR &= ~BIT(9);

	init_smart_lcd_8230();
#endif
#ifdef BKLIGHT_ON
	__gpio_set_pin(GPIOC, 15);
	__gpio_as_output(GPIOC, 15);
#endif

	pll_init();
	SERIAL_PUTS_ARGI("PLL running at ", __cpm_get_pllout() / 1000000, " MHz.\n");

	sdram_init();
	SERIAL_PUTS_ARGI("SDRAM running at ", __cpm_get_mclk() / 1000000, " MHz.\n");
	SERIAL_PUTS_ARGI("SDRAM size is ", get_memory_size() / 1048576, " MiB.\n");

	/* Ungate MSC0/1 clock */
	REG_CPM_CLKGR &= ~(BIT(6) | BIT(16));

	/* Set divider for the MSC0/1 clock */
	__cpm_set_mscdiv((__cpm_get_pllout2() / 24000000) - 1);
}

#ifdef USE_NAND
void nand_wait_ready(void)
{
	unsigned int timeout = 10000;

	while (__gpio_get_pin(GPIOC, 27) && timeout--);
	while (!__gpio_get_pin(GPIOC, 27));
}

void nand_init(void)
{
	REG32(EMC_SMCR1) = (EMC_TAS << EMC_SMCR_TAS_BIT) |
			   (EMC_TAH << EMC_SMCR_TAH_BIT) |
			   (EMC_TBP << EMC_SMCR_TBP_BIT) |
			   (EMC_TAW << EMC_SMCR_TAW_BIT) |
			   (EMC_STRV << EMC_SMCR_STRV_BIT);
}
#endif
