/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The TRS jack's MIDI OUT as usb.c (mo_trs) sees it on the SMK-37 Pro, for the tests that include usb.c alone: no UART,
 * the bytes go nowhere (hal/smk_uart_tx.h) */
static void smk_uart_tx_put(uint32_t b) { (void)b; }
static void smk_uart_tx_rt(uint32_t b) { (void)b; }
#define SMK_UT_TX_RING 128u
static uint8_t smk_ut_w, smk_ut_r;
