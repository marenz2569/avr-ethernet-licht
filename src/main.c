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

const uint8_t myip[]      = {172,23,92,247},
              mymac[]     = {0x70,0x69,0x69,0x2D,0x30,0x31},
              broadcast[] = {172,23,92,255};

volatile uint8_t change       = 1,
                 modi         = 'n',
                 oldModi      = 'n',
                 animation    = 1,
                 oldAnimation = 1,
                 dir          = 1;

#define NAME(name) \
        #name

#define MACRO_TO_STRING(x) \
        NAME(x)

#define err(e) \
        send_reply('e', e)

#define ack(m) \
        send_reply(m, "")

#define checklen(x) \
        (cmdlen >= x)

#define LEDS_LOOP_BEGIN \
        i = ws2812_LEDS; \
        while(i--) {

#define LEDS_LOOP_END \
        }

void send_reply(char command, char message[])
{
	enc28j60_buffer[UDP_DATA_P] = command;
	uint16_t len = strlen(message);
	enc28j60_buffer[UDP_DATA_P+1] = len >> 8;
	enc28j60_buffer[UDP_DATA_P+2] = len;
	memcpy(enc28j60_buffer + UDP_DATA_P + 3, message, len);
	makeUdpReply(len + 3, mymac, myip, 49152);
}

void main(void)
{
	DDRB |= _BV(PORTB0);

	while (!enc28j60_init(mymac))
		_delay_ms(100);

	sei();
	EIMSK = _BV(INT0);

	PORTB |= _BV(PORTB0);

	ws2812_locked = 0;

	rgb color;

#define break_on_change while (!change) \
                                _delay_us(0.0000000000000625); \
                        break

	uint16_t i,
	         x0e_step_count,
	         x0e_steps_for_360;

	float x0e_led_shift,
	      x0e_step_per_cycle;

	x0e_step_per_cycle = 0.625;
	x0e_steps_for_360 = 360.0 / x0e_step_per_cycle;
	x0e_led_shift = 360.0  / (float) ws2812_LEDS;

	for (;;) {
		switch (oldModi = modi) {
		case 'a':
		case 'r':
		case 's':
			if (change) {
				ws2812_locked = 0;
				ws2812_sync();
				change = 0;
			}
			break_on_change;
		case 'n':
			switch (animation) {
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
}

/* enc28j60 interrupt */
ISR(INT0_vect)
{
	uint16_t plen, i, datalen, cmdlen, offset, start;
	uint8_t *data;
	struct {
		uint8_t id[2];
		rgb color;
	} s_led;
	char leds_string[5],
	     max_protolen_string[5];

	sprintf(leds_string, "%d", ws2812_LEDS);
	sprintf(max_protolen_string, "%d", ENC28J60_MAX_DATALEN);

	enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, EIE, EIE_INTIE);

	/* parse individual packets */
	while (enc28j60_readReg(EPKTCNT)) {
		plen = enc28j60_packetReceive();

		if (eth_type_is_arp_and_my_ip(plen, myip) &&
		    enc28j60_buffer[ETH_ARP_OPCODE_L_P] == ETH_ARP_OPCODE_REQ_L_V) {
			make_arp_answer_from_request(mymac, myip);
		} else if (eth_type_is_ip_and_my_ip(plen, myip, broadcast) &&
		           enc28j60_buffer[IP_PROTO_P] == IP_PROTO_UDP_V &&
		           enc28j60_buffer[UDP_DST_PORT_H_P] == 0xc0 && enc28j60_buffer[UDP_DST_PORT_L_P] == 0x00) {
			datalen = (uint16_t) (enc28j60_buffer[UDP_LEN_H_P] << 8) + enc28j60_buffer[UDP_LEN_L_P];
			datalen = (datalen>(ENC28J60_BUFFERSIZE)?(ENC28J60_BUFFERSIZE):datalen);
			datalen -= UDP_HEADER_LEN;
			data = enc28j60_buffer + UDP_DATA_P;
			cmdlen = data[2] | data[1] << 8;
			if (datalen > 2 && cmdlen == (datalen - 3)) {
				switch (modi = data[0]) {
				/* allset */
				case 'a':
					if (!checklen(3)) {
						err("need at least 3 arguments");
						break;
					}
					rgb color;
					memcpy(&color, data + 3, 3);
					ws2812_locked = 0;
					LEDS_LOOP_BEGIN
						ws2812_set_rgb_at(i, color);
					LEDS_LOOP_END;
					ws2812_locked = 1;
					change = 1;
					ack('a');
					break;
				/* information */
				sprintf(leds_string, "%d", ws2812_LEDS);
				case 'i':
					modi = oldModi;
					send_reply('i', strcat(strcat(strcat(strcat("{\"name\":\"frickel\",\"leds\":", leds_string), ",\"max_protolen\":"), max_protolen_string), "}"));
					break;
				case 'n':
					if (!checklen(1)) {
						err("protocol error");
						break;
					}
					switch (animation = data[3]) {
					case 0x01:
						if (!checklen(2))
							break;
						if (data[4]) {
							dir = 1;
						} else {
							dir = 0;
						}
						ws2812_locked = 1;
						change = 1;
						ack('n');
						break;
					default:
						animation = oldAnimation;
						err("mode not implemented");
						break;
					}
					break;
				/* rangeset */
				case 'r':
					if (!checklen(5)) {
						err("need at least 5 arguments");
						break;
					}
					offset = (uint16_t) (data[3] >> 8) | data[4];
					if (!(offset < ws2812_LEDS)) {
						err("out of range");
					}
					cmdlen -= 2;
					cmdlen -= cmdlen % 3 + 1;
					start = enc28j60_curPacketPointer + 6 + UDP_DATA_P + 5;
					enc28j60_dma(start, start + cmdlen, offset * 3 + ENC28J60_HEAP_START);
					ws2812_locked = 1;
					change = 1;
					ack('r');
					break;
				/* set */
				case 's':
					if (!checklen(5)) {
						err("need at least 5 arguments");
						break;
					}
					cmdlen -= cmdlen % 5;
					cmdlen = cmdlen / 5;
					enc28j60_writeReg16(ERDPTL, enc28j60_curPacketPointer + 6 + UDP_DATA_P + 3);
					while (cmdlen--) {
						enc28j60_readBuf(5, (uint8_t *) &s_led);
						ws2812_set_rgb_at(s_led.id[0] << 8 | s_led.id[1], s_led.color);
					}
					ws2812_locked = 1;
					change = 1;
					ack('s');
					break;
				default:
					modi = oldModi;
					err("mode not implemented");
				}
			} else {
				err("protocol error");
			}
		} else {
			/* ignore packet */
		}

		enc28j60_freePacketSpace();
	}

	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE);
}
