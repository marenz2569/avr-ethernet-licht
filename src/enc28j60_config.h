#ifndef ENC28J60_CONFIG_H__
#define ENC28J60_CONFIG_H__

#include "config.h"

#define ENC28J60_CS_DDR		DDRB
#define ENC28J60_CS_PORT	PORTB
#define ENC28J60_CS_PIN		PORTB2

enum {
	ENC28J60_BUFFERSIZE = 200
};

enum {
	ENC28J60_RX_START = 0,
	ENC28J60_RX_END   = 0x1f00
};

enum {
	ENC28J60_TX_START = 0x1f01,
	ENC28J60_TX_END   = 0x1fff
};

#endif
