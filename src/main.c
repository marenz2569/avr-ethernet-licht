#include <stdint.h>
#include <util/delay.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>

#include "ws2812.h"
#include "ws2812_config.h"
#include "enc28j60.h"
#include "spi.h"
#include "enc28j60_defs.h"
#include "ethernet_protocols.h"
#include "tick.h"

const uint8_t myip[]      = {172,23,92,15},
              mymac[]     = {0x70,0x69,0x69,0x2D,0x30,0x31},
              broadcast[] = {172,23,92,255};

volatile uint8_t change       = 1,
                 modi         = 'n',
                 animation    = 1,
                 dir          = 1;

/* in ms */
#define LOCK_TIMEOUT 50

#define NAME(name) \
        #name

#define MACRO_TO_STRING(x) \
        NAME(x)

/* send error message and close tcp session */
#define ERR(e) \
        plen = send_reply_P('e', PSTR(e)); \
	flags = TCP_FLAGS_FIN_V

/* send ok message */
#define OK \
        plen = send_reply_P(modi, PSTR("")); \
	ws2812_locked = 1; \
	change = 1

#define checklen(x) \
        (cmdlen >= x)

#define LEDS_LOOP_BEGIN \
        i = ws2812_LEDS; \
        while(i--) {

#define LEDS_LOOP_END \
        }

uint16_t send_reply_P(const char command, const char *message)
{
	enc28j60_buffer[TCP_OPTIONS_P] = command;
	uint16_t len = strlen_P(message);
	enc28j60_buffer[TCP_OPTIONS_P+1] = len >> 8;
	enc28j60_buffer[TCP_OPTIONS_P+2] = len;
	memcpy_P(enc28j60_buffer + TCP_OPTIONS_P + 3, message, len);
	return len + 3;
}

int main(void)
{
	tick_init();

	DDRB |= _BV(PORTB0);

	while (!enc28j60_init(mymac))
		_delay_ms(100);

	sei();
	EIMSK = _BV(INT0);

	PORTB |= _BV(PORTB0);

	ws2812_locked = 0;

	rgb color;

	uint16_t i,
	         x0e_step_count,
	         x0e_steps_for_360;

	float x0e_led_shift,
	      x0e_step_per_cycle;

	x0e_step_per_cycle = 0.625;
	x0e_steps_for_360 = 360.0 / x0e_step_per_cycle;
	x0e_led_shift = 360.0  / (float) ws2812_LEDS;

	for (;;) {
		switch (modi) {
		/*
		 * ALLSET
		 * C: "a" + LEN + GRB (3 Bytes)
		 * S: "a" + LEN (0x0000) || "e" + LEN + ERROR
		 */
		case 'a':
		/*
		 * RANGESET
		 * C: "r" + LEN + offset (2 Byte) + GRB (3 Byte) ...
		 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
		 */
		case 'r':
		/*
		 * SET
		 * C: "s" + LEN + [ledid (2 Byte) + GRB (3 Byte)] ...
		 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
		 */
		case 's':
			if (change) {
				ws2812_locked = 0;
				ws2812_sync();
				change = 0;
			}
			while (!change) {
                                _delay_us(0.0000000000000625);
			}
                        break;
		case 'n':
			switch (animation) {
			/*
			 * FARBVERLAUF
			 * C: "n" + LEN (0x0002) + 0x0100
			 * oder
			 * C: "n" + LEN (0x0002) + 0x01FF
			 */
			case 1:
				if (change) {
					x0e_step_count = 0;
					ws2812_locked = 0;
					change = 0;
				}
				while (!change) {
					LEDS_LOOP_BEGIN
						if (dir) {
							color = hsi2rgb(x0e_led_shift * (float) (i + 1) + (float) x0e_step_count * x0e_step_per_cycle, 1.0, 1.0);
						} else {
							color = hsi2rgb(x0e_led_shift * (float ) (ws2812_LEDS - i) + (float) x0e_step_count * x0e_step_per_cycle, 1.0, 1.0);
						}
						ws2812_set_rgb_at(i, color);
					LEDS_LOOP_END;
					ws2812_sync();
					x0e_step_count = ++x0e_step_count % x0e_steps_for_360;
				}
				break;
			}
			break;
		}
	}

	return 0;
}

#if 0
/* doorbell fun */
ISR(INT1_vect)
{
	uint16_t i, j;
	rgb c;

	for (j=6; j>0; j--) {
		LEDS_LOOP_BEGIN
			if (i%3 == 0) {
				c.g = 0xff;
			} else {
				c >>= 8;
			}
			ws2812_set_rgb_at(i, c);
		LEDS_LOOP_END;
		_delay_ms(50);
	}
}
#endif

struct {
	uint64_t time;
	uint32_t ip;
} lock = { .time = 0, .ip = 0 };

/* enc28j60 interrupt */
ISR(INT0_vect)
{
	uint16_t i, plen, data_offset, datalen, cmdlen, offset, start;
	uint8_t flags;
	uint8_t *data;
	struct {
		uint8_t id[2];
		rgb color;
	} pixel;

	enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, EIE, EIE_INTIE);

	/* parse individual packets */
	while (enc28j60_readReg(EPKTCNT)) {
		plen = enc28j60_packetReceive();

		/* process ARP packets */
		if (eth_type_is_arp_and_my_ip(plen, myip) &&
		    enc28j60_buffer[ETH_ARP_OPCODE_L_P] == ETH_ARP_OPCODE_REQ_L_V) {
			make_arp_answer_from_request(mymac, myip);
		/* proccess TCP/IPv4 packets */
		} else if (eth_type_is_ip_and_my_ip(plen, myip, broadcast) &&
		           enc28j60_buffer[IP_PROTO_P] == IP_PROTO_TCP_V &&
		           enc28j60_buffer[TCP_DST_PORT_H_P] == 0xc0 && enc28j60_buffer[TCP_DST_PORT_L_P] == 0x00) {
			/* timeout, acquire new lock */
			if (lock.time + LOCK_TIMEOUT <= systick) {
				memcpy(&lock.ip, enc28j60_buffer + IP_SRC_P, 4);
				memcpy(&lock.time, (void *) &systick, sizeof(systick));
			/* no timeout and wrong ip, reset connection */
			} else if (memcmp(&lock.ip, enc28j60_buffer + IP_SRC_P, 4) != 0) {
				make_tcp_ack(mymac, myip, 0, TCP_FLAGS_RST_V);
			/*
			 * no timeout and right ip (all of the following else if)
			 * start stream, synack
			 */
			} else if (enc28j60_buffer[TCP_FLAGS_P] & TCP_FLAGS_SYN_V) {
				make_tcp_synack(mymac, myip);
			/* stop stream after fin */
			} else if (enc28j60_buffer[TCP_FLAGS_P] & TCP_FLAGS_FIN_V) {
				make_tcp_ack(mymac, myip, 0, TCP_FLAGS_FIN_V);
			/* reply with ack */
			/* maybe I should resend the data if no ack is received after data being sent */
			//} else if (enc28j60_buffer[TCP_FLAGS_P] & TCP_FLAGS_ACK_V) {
			//	make_tcp_ack(mymac, myip, 0, 0);
			/* handle incoming data */
			} else if (enc28j60_buffer[TCP_FLAGS_P] & TCP_FLAGS_PUSH_V) {
				plen = 0;
				flags = 0;
				data_offset = TCP_SRC_PORT_H_P + get_tcp_header_len();
				datalen = get_tcp_data_len();
				data = enc28j60_buffer + data_offset;
				cmdlen = data[2] | data[1] << 8;
				if (datalen > 2 && cmdlen == (datalen - 3)) {
					switch (modi = data[0]) {
					/*
					 * ALLSET
					 * C: "a" + LEN + GRB (3 Bytes)
					 * S: "a" + LEN (0x0000) || "e" + LEN + ERROR
					 */
					case 'a':
						if (!checklen(3)) {
							ERR("protocol error");
							break;
						}
						rgb color;
						memcpy(&color, data + 3, 3);
						ws2812_locked = 0;
						LEDS_LOOP_BEGIN
							ws2812_set_rgb_at(i, color);
						LEDS_LOOP_END;
						OK;
						break;
					/*
					 * INFORMATION
					 * C: "i" + 0x00
					 * S: "i" + LEN + JSON-Beschreibung || "e" + LEN + ERROR
					 */
					case 'i':
						plen = send_reply_P('i', PSTR("{\"name\":\"frickel\",\"leds\":"MACRO_TO_STRING(ws2812_LEDS)",\"max_protolen\":"MACRO_TO_STRING(ENC28J60_MAX_DATALEN_M)",\"note\":\"Send all the data in one fucking packet!\"}"));
						break;
					/*
					 * FARBVERLAUF
					 * C: "n" + LEN (0x0002) + 0x0100
					 * oder
					 * C: "n" + LEN (0x0002) + 0x01FF
					 */
					case 'n':
						if (cmdlen < 1) {
							ERR("protocol error");
							break;
						}
						switch (animation = data[3]) {
						case 0x01:
							if (cmdlen < 2)
								ERR("protocol error");
								break;
							if (data[4]) {
								dir = 1;
							} else {
								dir = 0;
							}
							OK;
							break;
						default:
							ERR("protocol error");
							break;
						}
						break;
					/*
					 * RANGESET
					 * C: "r" + LEN + offset (2 Byte) + GRB (3 Byte) ...
					 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
					 */
					case 'r':
						offset = (uint16_t) (data[3] >> 8) | data[4];
						if (cmdlen < 5 ||
						    (cmdlen - 2) % 3 != 0 ||
						    offset >= ws2812_LEDS) {
							ERR("protocol error");
							break;
						}
						cmdlen = (cmdlen - 2) % 3 - 1;
						start = enc28j60_curPacketPointer + 6 + data_offset + 5;
						enc28j60_dma(start, start + cmdlen, offset * 3 + ENC28J60_HEAP_START);
						OK;
						break;
					/*
					 * SET
					 * C: "s" + LEN + [ledid (2 Byte) + GRB (3 Byte)] ...
					 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
					 */
					case 's':
						if (cmdlen < 5 && cmdlen % 5 != 0) {
							ERR("protocol error");
							break;
						}
						cmdlen /= 5;
						enc28j60_writeReg16(ERDPTL, enc28j60_curPacketPointer + 6 + data_offset + 3);
						while (cmdlen--) {
							enc28j60_readBuf(5, (uint8_t *) &pixel);
							ws2812_set_rgb_at(pixel.id[0] << 8 | pixel.id[1], pixel.color);
						}
						OK;
						break;
					default:
						ERR("protocol error");
					}
				} else {
					ERR("protocol error");
				}
				make_tcp_ack(mymac, myip, plen, flags);
			} else {
				/* some TCP flag I did not account for */
			}
		} else {
			/* unknown packet type, ignore */
		}

		enc28j60_freePacketSpace();
	}

	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE);
}
