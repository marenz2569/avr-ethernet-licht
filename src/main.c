#include <stdint.h>
#include <util/delay.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <math.h>

#include "ws2812.h"
#include "enc28j60.h"
#include "spi.h"
#include "enc28j60_defs.h"
#include "ethernet_protocols.h"
#include "config.h"

volatile uint8_t change       = 1,
                 modi         = 'n',
                 animation    = 1,
                 dir          = 1;
volatile float nx1_step_per_cycle = NX1_STEP_PER_CYCLE;

#define NAME(name) \
        #name

#define MACRO_TO_STRING(x) \
        NAME(x)

#define OK \
	ws2812_locked = 1; \
	change = 1; \
	plen = send_reply_P(modi, PSTR(""))

#define ERR \
        plen = send_reply_P('e', PSTR("protocol error"))

#define UNLOCK \
	ws2812_locked = 0; \
	change = 0

#define checklen(x) \
        (cmdlen >= x)

#define LEDS_LOOP_BEGIN \
        i = *ws2812_leds; \
        while(i--) {

#define LEDS_LOOP_END \
        }

uint16_t send_reply_P(const char command, const char *message)
{
	enc28j60_buffer[UDP_DATA_P] = command;
	uint16_t len = strlen_P(message);
	enc28j60_buffer[UDP_DATA_P+1] = len >> 8;
	enc28j60_buffer[UDP_DATA_P+2] = len;
	memcpy_P(enc28j60_buffer + UDP_DATA_P + 3, message, len);
	return len + 3;
}

int main(void)
{
	DDRB |= _BV(PORTB0);

	while (!enc28j60_init())
		_delay_ms(100);

	EIMSK = _BV(INT0);

	PORTB |= _BV(PORTB0);

	sei();

	UNLOCK;

	rgb pixel;

	uint8_t j;
	uint16_t i;

	float nx1_led_shift,
	      nx1_steps_for_360,
	      nx1_step_count;

	nx1_led_shift = 360.0  / (float) *ws2812_leds;

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
			UNLOCK;
			ws2812_sync();
			while (!change) {
			}
                        break;
		case 'n':
			switch (animation) {
			/*
			 * FARBVERLAUF
			 * C: "n" + LEN (0x0002) + 0x0100 + [NX1_STEP_PER_CYCLE]
			 * oder
			 * C: "n" + LEN (0x0002) + 0x01FF + [NX1_STEP_PER_CYCLE]
			 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
			 */
			case 1:
				UNLOCK;
				nx1_step_count = 0.0;
				nx1_steps_for_360 = 360.0 / nx1_step_per_cycle;
				while (!change) {
					LEDS_LOOP_BEGIN
						pixel = hsi2rgb(nx1_led_shift * (float) (dir?(i + 1):(*ws2812_leds - i)) + nx1_step_count * nx1_step_per_cycle, 1.0, 1.0);
						ws2812_set_rgb_at(i, &pixel);
					LEDS_LOOP_END;
					ws2812_sync();
					nx1_step_count = fmod(++nx1_step_count, nx1_steps_for_360);
				}
				break;
			/*
			 * KLINGEL
			 * C: "n" + LEN (0x0001) + 0x02
			 */
			case 2:
				UNLOCK;
				j = 0;
				while (!change) {
					*(&pixel.g+j) = 0xff; 
					*(&pixel.g+(j%3==0?1:0)) = 0; 
					*(&pixel.g+(j%3==2?1:2)) = 0; 
					LEDS_LOOP_BEGIN
						ws2812_set_rgb_at(i, &pixel);
					LEDS_LOOP_END;
					ws2812_sync();
					if (++j>=3) {
						j = 0;
					}
					_delay_ms(40);
				}
				break;
			/*
			 * BLAU/WEISZ
			 * C: "n" + LEN (0x0001) + 0x03
			 */
			case 3:
				UNLOCK;
				j = 0;
				while (!change) {
					pixel.r = 0;
					pixel.g = pixel.r;
					pixel.b = (j%2)?0:0xff;
					LEDS_LOOP_BEGIN
						ws2812_set_rgb_at(i, &pixel);
					LEDS_LOOP_END;
					ws2812_sync();
					if (++j>=2) {
						j=0;
					}
					_delay_ms(100);
				}
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

/* enc28j60 interrupt */
ISR(INT0_vect)
{
	uint16_t i, plen, datalen, data_offset, cmdlen, offset, start;
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
		if (eth_type_is_arp_and_my_ip(plen) &&
		    enc28j60_buffer[ETH_ARP_OPCODE_L_P] == ETH_ARP_OPCODE_REQ_L_V) {
			make_arp_answer_from_request();
		/* proccess TCP/IPv4 packets */
		} else if (eth_type_is_ip_and_my_ip(plen) &&
		           enc28j60_buffer[IP_PROTO_P] == IP_PROTO_UDP_V &&
		           enc28j60_buffer[UDP_DST_PORT_H_P] == 0xc0 && enc28j60_buffer[UDP_DST_PORT_L_P] == 0x00 &&
		           (0 == check_checksum(UDP_CHECKSUM_H_P, IP_SRC_P, ((uint16_t) enc28j60_buffer[UDP_LEN_L_P] | (enc28j60_buffer[UDP_LEN_H_P] << 8)) + 8, 1)) &&
		           (0 == check_checksum(IP_CHECKSUM_P, IP_P, IP_HEADER_LEN, 0))) {
			plen = 0;
			datalen = ((uint16_t) enc28j60_buffer[UDP_LEN_L_P] | (enc28j60_buffer[UDP_LEN_H_P] << 8)) - UDP_HEADER_LEN;
			data_offset = UDP_DATA_P;
			data = enc28j60_buffer + data_offset;
			cmdlen = data[2] | data[1] << 8;
			if (datalen >= 3 && cmdlen == (datalen - 3)) {
				switch (modi = data[0]) {
				/*
				 * ALLSET
				 * C: "a" + LEN + GRB (3 Bytes)
				 * S: "a" + LEN (0x0000) || "e" + LEN + ERROR
				 */
				case 'a':
					if (!checklen(3)) {
						ERR;
						break;
					}
					memcpy(&pixel.color, data + 3, 3);
					ws2812_locked = 0;
					LEDS_LOOP_BEGIN
						ws2812_set_rgb_at(i, &pixel.color);
					LEDS_LOOP_END;
					OK;
					break;
				/*
				 * INFORMATION
				 * C: "i" + 0x0000
				 * S: "i" + LEN + #LEDs || "e" + LEN + ERROR
				 */
				case 'i':
					plen = sprintf(data + 3, "%u", *ws2812_leds);
					data[0] = 'i';
					data[1] = plen >> 8;
					data[2] = plen;
					plen += 3;
					break;
				case 'n':
					if (!checklen(1)) {
						ERR;
						break;
					}
					switch (animation = data[3]) {
					/*
					 * FARBVERLAUF
					 * C: "n" + LEN (0x0002) + 0x0100 + [NX1_STEP_PER_CYCLE]
					 * oder
					 * C: "n" + LEN (0x0002) + 0x01FF + [NX1_STEP_PER_CYCLE]
				 	 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
					 */
					case 1:
						if (!checklen(2)) {
							ERR;
							break;
						}
						if (checklen(6)) {
							for (i = 4; i > 0; i--) {
								*((uint8_t *) &nx1_step_per_cycle + i - 1) = *(data + 3 + 2 + 4 - i);
							}
						} else {
							nx1_step_per_cycle = NX1_STEP_PER_CYCLE;
						}
						if (data[4]) {
							dir = 1;
						} else {
							dir = 0;
						}
						OK;
						break;
					/*
					 * KLINGEL
					 * C: "n" + LEN (0x0001) + 0x02
					 */
					case 2:
					/*
					 * BLAU/WEISZ
					 * C: "n" + LEN (0x0001) + 0x03
					 */
					case 3:
						OK;
						break;
					default:
						ERR;
						break;
					}
					break;
				/*
				 * RANGESET
				 * C: "r" + LEN + offset (2 Byte) + GRB (3 Byte) ...
				 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
				 */
				case 'r':
					offset = (uint16_t) (data[3] << 8) | data[4];
					if (!checklen(5) ||
					    (cmdlen - 2) % 3 != 0 ||
					    (cmdlen - 2) % 3 + offset > *ws2812_leds) {
						ERR;
						break;
					}
					enc28j60_set_random_access(6 + data_offset + 5);
					enc28j60_readBuf(cmdlen - 2, (uint8_t *) (ws2812_buffer + offset * 3));
					OK;
					break;
				/*
				 * SET
				 * C: "s" + LEN + [ledid (2 Byte) + GRB (3 Byte)] ...
				 * S: "s" + LEN (0x0000) || "e" + LEN + ERROR
				 */
				case 's':
					if (!checklen(5) && cmdlen % 5 != 0) {
						ERR;
						break;
					}
					cmdlen /= 5;
					enc28j60_set_random_access(6 + data_offset + 3);
					while (cmdlen--) {
						enc28j60_readBuf(5, (uint8_t *) &pixel);
						ws2812_set_rgb_at(pixel.id[0] << 8 | pixel.id[1], &pixel.color);
					}
					OK;
					break;
				default:
					ERR;
					break;
				}
			} else {
				ERR;
			}
			makeUdpReply(plen, 49152);
		}

		enc28j60_freePacketSpace();
	}

	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE);
}
