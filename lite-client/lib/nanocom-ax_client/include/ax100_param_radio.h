#ifndef PARAM_RADIO_H_
#define PARAM_RADIO_H_
/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
 * NanoCom firmware
 *
 */

#include <stdint.h>
#include <param/param_types.h>

/* Common parameters for both RX and TX */
#define AX100_FREQ					0x00
#define AX100_BAUD					0x04
#define AX100_MODINDEX				0x08
#define AX100_GUARD					0x0C
#define AX100_PLLRANG_INIT			0x0E
#define AX100_FRM_MODE				0x0F

#define AX100_HMAC_ENABLE			0x10
#define AX100_RS_ENABLE				0x11
#define AX100_CRC32_ENABLE			0x12
#define AX100_RAND_ENABLE			0x13

#define AX100_HMAC_KEY				0x20
#define AX100_HMAC_SIZE				0x10

#define AX100_AX25_CALL				0x30
#define AX100_AX25_CALL_SIZE		0x6

/* TX only parameters */
#define AX100_TX_PREAMBLE			0x40
#define AX100_TX_PREAMBLE_LEN		0x41
#define AX100_TX_PREAMBLE_FLAGS		0x42
#define AX100_TX_INTERFRAME			0x43
#define AX100_TX_INTERFRAME_LEN		0x44
#define AX100_TX_INTERFRAME_FLAGS	0x45
#define AX100_TX_RSSI_BUSY			0x48
#define AX100_TX_KEY_UP_DELAY		0x4A
#define AX100_TX_PA_INPUT_LEVEL		0x4C
#define AX100_TX_BER				0x50
#define AX100_TX_PARAM_SIZE			0x60

/* RX only parameters */
#define AX100_RX_BW					0x40
#define AX100_RX_AFCRANGE			0x44

#define AX100_RX_PARAM_SIZE			0x50

#define AX100_HMACS_TABLE_COUNT     0x3F
#define AX100_HMACS_PARAM_SIZE      AX100_HMACS_TABLE_COUNT * AX100_HMAC_SIZE

#define COMMON_PARAMS \
	{.name = "freq", 			.addr = AX100_FREQ, 				.type = PARAM_UINT32, 	.size = sizeof(uint32_t)}, \
	{.name = "baud", 			.addr = AX100_BAUD, 				.type = PARAM_UINT32, 	.size = sizeof(uint32_t)}, \
	{.name = "modindex", 		.addr = AX100_MODINDEX, 			.type = PARAM_FLOAT, 	.size = sizeof(float)}, \
	{.name = "guard",			.addr = AX100_GUARD, 				.type = PARAM_UINT16, 	.size = sizeof(uint16_t)}, \
	{.name = "pllrang",			.addr = AX100_PLLRANG_INIT, 		.type = PARAM_UINT8, 	.size = sizeof(uint8_t)}, \
	{.name = "mode",			.addr = AX100_FRM_MODE, 			.type = PARAM_UINT8, 	.size = sizeof(uint8_t)}, \
	{.name = "csp_hmac",		.addr = AX100_HMAC_ENABLE,			.type = PARAM_BOOL, 	.size = sizeof(uint8_t)}, \
	{.name = "csp_rs",			.addr = AX100_RS_ENABLE,			.type = PARAM_BOOL, 	.size = sizeof(uint8_t)}, \
	{.name = "csp_crc",			.addr = AX100_CRC32_ENABLE,			.type = PARAM_BOOL, 	.size = sizeof(uint8_t)}, \
	{.name = "csp_rand",		.addr = AX100_RAND_ENABLE,			.type = PARAM_BOOL, 	.size = sizeof(uint8_t)}, \
	{.name = "csp_hmac_key",	.addr = AX100_HMAC_KEY,				.type = PARAM_DATA,		.size = AX100_HMAC_SIZE}, \
	{.name = "ax25_call",		.addr = AX100_AX25_CALL,			.type = PARAM_STRING,	.size = AX100_AX25_CALL_SIZE}, \

static const param_table_t ax100_tx_config[] = {
	COMMON_PARAMS
	{.name = "preamb",			.addr = AX100_TX_PREAMBLE, 			.type = PARAM_X8, 		.size = sizeof(uint8_t)},
	{.name = "preamblen",		.addr = AX100_TX_PREAMBLE_LEN, 		.type = PARAM_UINT8, 	.size = sizeof(uint8_t)},
	{.name = "preambflags",		.addr = AX100_TX_PREAMBLE_FLAGS,	.type = PARAM_UINT8, 	.size = sizeof(uint8_t)},
	{.name = "intfrm",			.addr = AX100_TX_INTERFRAME, 		.type = PARAM_X8, 		.size = sizeof(uint8_t)},
	{.name = "intfrmlen",		.addr = AX100_TX_INTERFRAME_LEN, 	.type = PARAM_UINT8, 	.size = sizeof(uint8_t)},
	{.name = "intfrmflags",		.addr = AX100_TX_INTERFRAME_FLAGS,	.type = PARAM_UINT8, 	.size = sizeof(uint8_t)},
	{.name = "rssibusy",		.addr = AX100_TX_RSSI_BUSY,			.type = PARAM_INT16, 	.size = sizeof(int16_t)},
	{.name = "kup_delay",		.addr = AX100_TX_KEY_UP_DELAY,		.type = PARAM_UINT8, 	.size = sizeof(uint8_t)},
	{.name = "pa_level",		.addr = AX100_TX_PA_INPUT_LEVEL,	.type = PARAM_X16, 		.size = sizeof(uint16_t)},
	{.name = "ber",				.addr = AX100_TX_BER,				.type = PARAM_FLOAT, 	.size = sizeof(float)},
};

static const param_table_t ax100_rx_config[] = {
	COMMON_PARAMS
	{.name = "bw", 				.addr = AX100_RX_BW, 				.type = PARAM_UINT32, 	.size = sizeof(uint32_t)},
	{.name = "afcrange", 		.addr = AX100_RX_AFCRANGE,			.type = PARAM_INT32, 	.size = sizeof(int32_t)},
};

static const param_table_t ax100_hmacs_config[] = {
    {.name = "hmac_key_00",     .addr = 0 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_01",     .addr = 1 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_02",     .addr = 2 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_03",     .addr = 3 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_04",     .addr = 4 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_05",     .addr = 5 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_06",     .addr = 6 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_07",     .addr = 7 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_08",     .addr = 8 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_09",     .addr = 9 * AX100_HMAC_SIZE,        .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_10",     .addr = 10 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_11",     .addr = 11 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_12",     .addr = 12 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_13",     .addr = 13 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_14",     .addr = 14 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_15",     .addr = 15 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_16",     .addr = 16 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_17",     .addr = 17 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_18",     .addr = 18 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_19",     .addr = 19 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_20",     .addr = 20 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_21",     .addr = 21 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_22",     .addr = 22 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_23",     .addr = 23 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_24",     .addr = 24 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_25",     .addr = 25 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_26",     .addr = 26 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_27",     .addr = 27 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_28",     .addr = 28 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_29",     .addr = 29 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_30",     .addr = 30 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_31",     .addr = 31 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_32",     .addr = 32 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_33",     .addr = 33 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_34",     .addr = 34 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_35",     .addr = 35 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_36",     .addr = 36 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_37",     .addr = 37 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_38",     .addr = 38 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_39",     .addr = 39 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_40",     .addr = 40 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_41",     .addr = 41 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_42",     .addr = 42 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_43",     .addr = 43 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_44",     .addr = 44 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_45",     .addr = 45 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_46",     .addr = 46 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_47",     .addr = 47 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_48",     .addr = 48 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_49",     .addr = 49 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_50",     .addr = 50 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_51",     .addr = 51 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_52",     .addr = 52 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_53",     .addr = 53 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_54",     .addr = 54 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_55",     .addr = 55 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_56",     .addr = 56 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_57",     .addr = 57 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_58",     .addr = 58 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_59",     .addr = 59 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_60",     .addr = 60 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_61",     .addr = 61 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE},
    {.name = "hmac_key_62",     .addr = 62 * AX100_HMAC_SIZE,       .type = PARAM_DATA,     .size = AX100_HMAC_SIZE}
};


static const int ax100_config_rx_count = sizeof(ax100_rx_config) / sizeof(ax100_rx_config[0]);
static const int ax100_config_tx_count = sizeof(ax100_tx_config) / sizeof(ax100_tx_config[0]);
static const int ax100_hmacs_config_count = sizeof(ax100_hmacs_config) / sizeof(ax100_hmacs_config[0]);

#endif /* PARAM_RADIO_H_ */
