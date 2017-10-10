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
#include "tick.h"

struct config {
	uint8_t modi,
	        animation,
	        dir;
	float nx1_step_per_cycle;
} config_global = {
	.modi = 'n',
	.animation = 1,
	.dir = 1,
	.nx1_step_per_cycle = NX1_STEP_PER_CYCLE
};

#define ERR \
        plen = send_reply_P('e', PSTR("protocol error")); \
				err = 1

#define checklen(x) \
        (cmdlen >= x)

#define LEDS_LOOP_BEGIN \
        i = *ws2812_leds; \
        while(i--) {

#define LEDS_LOOP_END \
        }

#define MODI_CHANGE(x) \
        if (0 != memcmp(&config_global, &config_old, sizeof(config_global))) { \
					memcpy(&config_old, &config_global, sizeof(config_global)); \
					async_set(0); \
        	x \
				}

#define ASYNC_EVERY(t, x) \
        if (systick >= async_delay) { \
        	x \
					async_set(t); \
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
	rgb pixel;

	struct config config_old = config_global;
	uint8_t j;
	uint16_t i;

	float nx1_led_shift,
	      nx1_steps_for_360,
	      nx1_step_count;

	nx1_led_shift = 360.0  / (float) *ws2812_leds;

	DDRB |= _BV(PORTB0);

	tick_init();

	while (!enc28j60_init())
		_delay_ms(100);

	EIMSK = _BV(INT0);
	EICRA = ~(_BV(ISC01) | _BV(ISC00));

	PORTB |= _BV(PORTB0);


	sei();

	for (;;) {
		switch (config_global.modi) {
		/* ALLSET */
		case 'a':
		/* RANGESET */
		case 'r':
		/* SET */
		case 's':
			ws2812_sync();
			break;
		case 'n':
			switch (config_global.animation) {
			/* FARBVERLAUF */
			case 1:
				MODI_CHANGE( \
					nx1_step_count = 0.0; \
					nx1_steps_for_360 = 360.0 / config_global.nx1_step_per_cycle; \
				)
				LEDS_LOOP_BEGIN
					pixel = hsi2rgb(nx1_led_shift * (float) (config_global.dir?(i + 1):(*ws2812_leds - i)) + nx1_step_count * config_global.nx1_step_per_cycle, 1.0, 1.0);
					ws2812_set_rgb_at(i, &pixel);
				LEDS_LOOP_END;
				ws2812_sync();
				nx1_step_count = fmod(++nx1_step_count, nx1_steps_for_360);
				break;
			/* KLINGEL */
			case 2:
				MODI_CHANGE( \
					j = 0; \
				)
				ASYNC_EVERY(40, \
					*(&pixel.g+j) = 0xff; \
					*(&pixel.g+(j%3==0?1:0)) = 0; \
					*(&pixel.g+(j%3==2?1:2)) = 0; \
					LEDS_LOOP_BEGIN \
						ws2812_set_rgb_at(i, &pixel); \
					LEDS_LOOP_END; \
					ws2812_sync(); \
					if (++j>=3) { \
						j = 0; \
					} \
				)
				break;
			/* BLAU/WEISZ */
			case 3:
				MODI_CHANGE( \
					j = 0; \
				)
				ASYNC_EVERY(100, \
					pixel.r = 0; \
					pixel.g = pixel.r; \
					pixel.b = (j%2)?0:0xff; \
					LEDS_LOOP_BEGIN \
						ws2812_set_rgb_at(i, &pixel); \
					LEDS_LOOP_END; \
					ws2812_sync(); \
					if (++j>=2) { \
						j=0; \
					} \
				)
				break;
			default:
				break;
			}
		default:
			break;
		}
	}
}

/* enc28j60 interrupt */
ISR(INT0_vect)
{
	uint16_t i, plen, datalen, data_offset, cmdlen, offset;
	uint8_t err = 0;
	uint8_t *data;
	struct {
		uint8_t id[2];
		rgb color;
	} pixel;
	static uint64_t last_packet_tick = 0;

	struct config config_local = config_global;

	/* parse individual packets */
	while (enc28j60_readReg(EPKTCNT)) {
		plen = enc28j60_packetReceive();

		/* process ARP packets */
		if (eth_type_is_arp_and_my_ip(plen) &&
		    enc28j60_buffer[ETH_ARP_OPCODE_L_P] == ETH_ARP_OPCODE_REQ_L_V) {
			make_arp_answer_from_request();
		/* proccess UDP/IPv4 packets */
		} else if (eth_type_is_ip_and_my_ip(plen) &&
		           enc28j60_buffer[IP_PROTO_P] == IP_PROTO_UDP_V &&
		           enc28j60_buffer[UDP_DST_PORT_H_P] == 0xc0 && enc28j60_buffer[UDP_DST_PORT_L_P] == 0x00 &&
							 last_packet_tick + 33 <= systick) {
			last_packet_tick = systick;
			plen = 0;
			datalen = ((uint16_t) enc28j60_buffer[UDP_LEN_L_P] | (enc28j60_buffer[UDP_LEN_H_P] << 8)) - UDP_HEADER_LEN;
			data_offset = UDP_DATA_P;
			data = enc28j60_buffer + data_offset;
			cmdlen = data[2] | data[1] << 8;
			if (datalen >= 3 && cmdlen == (datalen - 3)) {
				switch (config_local.modi = data[0]) {
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
					LEDS_LOOP_BEGIN
						ws2812_set_rgb_at(i, &pixel.color);
					LEDS_LOOP_END;
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
					switch (config_local.animation = data[3]) {
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
								*((uint8_t *) &(config_local.nx1_step_per_cycle) + i - 1) = *(data + 3 + 2 + 4 - i);
							}
						} else {
							config_local.nx1_step_per_cycle = NX1_STEP_PER_CYCLE;
						}
						config_local.dir = data[4];
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
					break;
				default:
					ERR;
					break;
				}
			} else {
				ERR;
			}
			if (!err) {
				memcpy(&config_global, &config_local, sizeof(config_global));
			}
			makeUdpReply(plen, 49152);
		}

		enc28j60_freePacketSpace();
	}
}

void user_tick_interrupt(void)
{
}
