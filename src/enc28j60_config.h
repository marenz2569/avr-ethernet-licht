#ifndef ENC28J60_CONFIG_H__
#define ENC28J60_CONFIG_H__

#define ENC28J60_CS_DDR		DDRB
#define ENC28J60_CS_PORT	PORTB
#define ENC28J60_CS_PIN		PORTB2

enum {
	ENC28J60_BUFFERSIZE = 200
};

#define ENC28J60_MAX_FRAMELEN_M 966

/* 4.5kb receive buffer */
enum {
	ENC28J60_RX_START = 0,
	ENC28J60_RX_END   = ((6 + ENC28J60_MAX_FRAMELEN_M) * 3 + 1)
};

/* 0.5kb transmit buffer */
enum {
	ENC28J60_TX_START = 2918,
	ENC28J60_TX_END   = 3019
};

/* 3kb heap */
enum {
	ENC28J60_HEAP_START = 3020,
	ENC28J60_HEAP_END   = 0x1FFF
};

#endif
