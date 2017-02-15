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

volatile uint8_t change       = 1,
                 modi         = 'n',
                 animation    = 1,
                 dir          = 1;

#define NAME(name) \
        #name

#define MACRO_TO_STRING(x) \
        NAME(x)

#define OK \
	ws2812_locked = 1; \
	change = 1; \
	send_reply_P(modi, PSTR(""))

#define ERR \
        send_reply_P('e', PSTR("protocol error"))

#define UNLOCK \
	ws2812_locked = 0; \
	change = 0

#define checklen(x) \
        (cmdlen >= x)

#define LEDS_LOOP_BEGIN \
        i = WS2812_LEDS; \
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

	sei();
	EIMSK = _BV(INT0);

	PORTB |= _BV(PORTB0);

	UNLOCK;

	rgb pixel;

	uint8_t j;
	uint16_t i,
	         x0e_step_count,
	         x0e_steps_for_360;

	float x0e_led_shift,
	      x0e_step_per_cycle;

	x0e_step_per_cycle = 0.625;
	x0e_steps_for_360 = 360.0 / x0e_step_per_cycle;
	x0e_led_shift = 360.0  / (float) WS2812_LEDS;

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
				UNLOCK;
				x0e_step_count = 0;
				while (!change) {
					LEDS_LOOP_BEGIN
						pixel = hsi2rgb(x0e_led_shift * (float) (dir?(i + 1):(WS2812_LEDS - i)) + (float) x0e_step_count * x0e_step_per_cycle, 1.0, 1.0);
						ws2812_set_rgb_at(i, &pixel);
					LEDS_LOOP_END;
					ws2812_sync();
					x0e_step_count = ++x0e_step_count % x0e_steps_for_360;
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
	static struct {
		uint64_t time;
		uint32_t ip;
	} lock = { .time = 0, .ip = 0 };

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
		           enc28j60_buffer[UDP_DST_PORT_H_P] == 0xc0 && enc28j60_buffer[UDP_DST_PORT_L_P] == 0x00) {
			plen = 0;
			datalen = (uint16_t) enc28j60_buffer[UDP_LEN_L_P] | (enc28j60_buffer[UDP_LEN_H_P] << 8);
			data_offset = UDP_DATA_P;
			data = enc28j60_buffer + data_offset;
			cmdlen = data[2] | data[1] << 8;
			if (datalen > 3 && cmdlen == (datalen - 3)) {
				switch (modi = data[0]) {
				/*
				 * ALLSET
				 * C: "a" + LEN + GRB (3 Bytes)
				 * S: "a" + LEN (0x0000) || "e" + LEN + ERROR
				 */
				case 'a':
					if (cmdlen < 3) {
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
				 * C: "i" + 0x00
				 * S: "i" + LEN + JSON-Beschreibung || "e" + LEN + ERROR
				 */
				case 'i':
					plen = send_reply_P('i', PSTR("{\"name\":\"frickel\",\"leds\":"MACRO_TO_STRING(WS2812_LEDS)",\"max_protolen\":"MACRO_TO_STRING(ENC28J60_MAX_DATALEN_M)",\"note\":\"Send all the data in one fucking packet!\"}"));
					break;
				case 'n':
					if (cmdlen < 1) {
						ERR;
						break;
					}
					switch (animation = data[3]) {
					/*
					 * FARBVERLAUF
					 * C: "n" + LEN (0x0002) + 0x0100
					 * oder
					 * C: "n" + LEN (0x0002) + 0x01FF
					 */
					case 1:
						if (cmdlen < 2) {
							ERR;
							break;
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
					offset = (uint16_t) (data[3] >> 8) | data[4];
					if (cmdlen < 5 ||
					    (cmdlen - 2) % 3 != 0 ||
					    (cmdlen - 2) % 3 + offset > WS2812_LEDS) {
						ERR;
						break;
					}
					cmdlen = (cmdlen - 2) / 3 - 1;
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
						ERR;
						break;
					}
					cmdlen /= 5;
					enc28j60_writeReg16(ERDPTL, enc28j60_curPacketPointer + 6 + data_offset + 3);
					while (cmdlen--) {
						enc28j60_readBuf(5, (uint8_t *) &pixel);
						ws2812_set_rgb_at(pixel.id[0] << 8 | pixel.id[1], &pixel.color);
					}
					OK;
					break;
				default:
					ERR;
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
