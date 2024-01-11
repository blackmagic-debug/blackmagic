/*
 * renesas.h
 *
 *  Created on: Jan 9, 2024
 *      Author: mircfr
 */

#ifndef SRC_TARGET_RENESAS_RA_H_
#define SRC_TARGET_RENESAS_RA_H_

#define PNR_SERIES(pnr3, pnr4, pnr5, pnr6) (((pnr3) << 24U) | ((pnr4) << 16U) | ((pnr5) << 8U) | (pnr6))

typedef enum {
	PNR_SERIES_RA2L1 = PNR_SERIES('A', '2', 'L', '1'),
	PNR_SERIES_RA2E1 = PNR_SERIES('A', '2', 'E', '1'),
	PNR_SERIES_RA2E2 = PNR_SERIES('A', '2', 'E', '2'),
	PNR_SERIES_RA2A1 = PNR_SERIES('A', '2', 'A', '1'),
	PNR_SERIES_RA4M1 = PNR_SERIES('A', '4', 'M', '1'),
	PNR_SERIES_RA4M2 = PNR_SERIES('A', '4', 'M', '2'),
	PNR_SERIES_RA4M3 = PNR_SERIES('A', '4', 'M', '3'),
	PNR_SERIES_RA4E1 = PNR_SERIES('A', '4', 'E', '1'),
	PNR_SERIES_RA4E2 = PNR_SERIES('A', '4', 'E', '2'),
	PNR_SERIES_RA4W1 = PNR_SERIES('A', '4', 'W', '1'),
	PNR_SERIES_RA6M1 = PNR_SERIES('A', '6', 'M', '1'),
	PNR_SERIES_RA6M2 = PNR_SERIES('A', '6', 'M', '2'),
	PNR_SERIES_RA6M3 = PNR_SERIES('A', '6', 'M', '3'),
	PNR_SERIES_RA6M4 = PNR_SERIES('A', '6', 'M', '4'),
	PNR_SERIES_RA6M5 = PNR_SERIES('A', '6', 'M', '5'),
	PNR_SERIES_RA6E1 = PNR_SERIES('A', '6', 'E', '1'),
	PNR_SERIES_RA6E2 = PNR_SERIES('A', '6', 'E', '2'),
	PNR_SERIES_RA6T1 = PNR_SERIES('A', '6', 'T', '1'),
	PNR_SERIES_RA6T2 = PNR_SERIES('A', '6', 'T', '2'),
} renesas_pnr_series_e;

typedef enum flash_mode_ {
	FLASH_VERSION_MF3=3,
	FLASH_VERSION_MF4=4,
} flash_version_e;

typedef struct renesas_priv {
	uint8_t pnr[17]; /* 16-byte PNR + 1-byte null termination */
	renesas_pnr_series_e series;
	target_addr_t flash_root_table; /* if applicable */
	flash_version_e flash_version;
	bool flash_cache;
	bool pre_fetch_buffer;
} renesas_priv_s;

#endif /* SRC_TARGET_RENESAS_RA_H_ */
