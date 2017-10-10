#include <stdlib.h>
#include <string.h>
#include <avr/interrupt.h>

#include "spi.h"
#include "enc28j60.h"
#include "enc28j60_defs.h"
#include "enc28j60_config.h"
#include "ethernet_protocols.h"
#include "ethernet_protocols_config.h"

#define FILL16(to, data) \
        enc28j60_buffer[to] = (data) >> 8; \
        enc28j60_buffer[to+1] = data

const uint8_t ip[]        = IP,
              broadcast[] = BROADCAST;

uint8_t eth_type_is_arp_and_my_ip(const uint16_t len)
{
	return len >= 42 && enc28j60_buffer[ETH_TYPE_H_P] == ETHTYPE_ARP_H_V &&
	       enc28j60_buffer[ETH_TYPE_L_P] == ETHTYPE_ARP_L_V &&
	       memcmp(enc28j60_buffer + ETH_ARP_DST_IP_P, ip, 4) == 0;
}

void make_arp_answer_from_request(void)
{
	memcpy(enc28j60_buffer + ETH_DST_MAC, enc28j60_buffer + ETH_SRC_MAC, 6);
	memcpy(enc28j60_buffer + ETH_SRC_MAC, mac, 6);
	enc28j60_buffer[ETH_ARP_OPCODE_H_P] = ETH_ARP_OPCODE_REPLY_H_V;
	enc28j60_buffer[ETH_ARP_OPCODE_L_P] = ETH_ARP_OPCODE_REPLY_L_V;
	memcpy(enc28j60_buffer + ETH_ARP_DST_MAC_P, enc28j60_buffer + ETH_ARP_SRC_MAC_P, 6);
	memcpy(enc28j60_buffer + ETH_ARP_SRC_MAC_P, mac, 6);
	memcpy(enc28j60_buffer + ETH_ARP_DST_IP_P, enc28j60_buffer + ETH_ARP_SRC_IP_P, 4);
	memcpy(enc28j60_buffer + ETH_ARP_SRC_IP_P, ip, 4);
	enc28j60_packetSend(42);
}

uint8_t eth_type_is_ip_and_my_ip(const uint16_t len)
{
	const uint8_t allOnes[] = {0xff, 0xff, 0xff, 0xff};

	return len >= 42 && enc28j60_buffer[ETH_TYPE_H_P] == ETHTYPE_IP_H_V &&
	       enc28j60_buffer[ETH_TYPE_L_P] == ETHTYPE_IP_L_V &&
	       enc28j60_buffer[IP_HEADER_LEN_VER_P] == 0x45 &&
	       (memcmp(enc28j60_buffer + IP_DST_P, ip, 4) == 0 ||
	        memcmp(enc28j60_buffer + IP_DST_P, broadcast, 4) == 0 ||
	        memcmp(enc28j60_buffer + IP_DST_P, allOnes, 4) == 0);
}

static void fill_checksum(uint8_t dest, const uint8_t off, uint16_t len, const uint8_t type)
{
	const uint8_t *ptr = enc28j60_buffer + off;
	uint32_t sum = type==1?IP_PROTO_UDP_V+len-8:0;

	enc28j60_buffer[dest] = 0x00;
	enc28j60_buffer[dest + 1] = 0x00;

	while (len > 1) {
		sum += (uint16_t) (((uint16_t)*ptr<<8)|*(ptr+1));
		ptr += 2;
		len -= 2;
	}
	if (len) {
		sum += ((uint32_t)*ptr) << 8;
	}
	while (sum >> 16) {
		sum = (uint16_t) sum + (sum >> 16);
	}
	uint16_t ck = ~(uint16_t)sum;
	enc28j60_buffer[dest] = ck >> 8;
	enc28j60_buffer[dest + 1] = ck;
}

static void make_ip_checksum(void)
{
	enc28j60_buffer[IP_FLAGS_P] = 0x40;
	enc28j60_buffer[IP_FLAGS_P + 1] = 0;
	enc28j60_buffer[IP_TTL_P] = 64;
	fill_checksum(IP_CHECKSUM_P, IP_P, IP_HEADER_LEN, 0);
}

static void make_return_packet(void)
{
	memcpy(enc28j60_buffer + ETH_DST_MAC, enc28j60_buffer + ETH_SRC_MAC, 6);
        memcpy(enc28j60_buffer + ETH_SRC_MAC, mac, 6);
        memcpy(enc28j60_buffer + IP_DST_P, enc28j60_buffer + IP_SRC_P, 4);
        memcpy(enc28j60_buffer + IP_SRC_P, ip, 4);
}

void makeUdpReply(uint16_t datalen, const uint16_t port)
{
	datalen = datalen>ENC28J60_BUFFERSIZE?ENC28J60_BUFFERSIZE:datalen;
	enc28j60_buffer[IP_TOTLEN_H_P] = (IP_HEADER_LEN + UDP_HEADER_LEN + datalen) >> 8;
	enc28j60_buffer[IP_TOTLEN_L_P] = IP_HEADER_LEN + UDP_HEADER_LEN + datalen;
	make_return_packet();

	make_ip_checksum();

	memcpy(enc28j60_buffer + UDP_DST_PORT_H_P, enc28j60_buffer + UDP_SRC_PORT_H_P, 2);
	enc28j60_buffer[UDP_SRC_PORT_H_P] = port >> 8;
	enc28j60_buffer[UDP_SRC_PORT_L_P] = port;
	enc28j60_buffer[UDP_LEN_H_P] = (UDP_HEADER_LEN + datalen) >> 8;
	enc28j60_buffer[UDP_LEN_L_P] = UDP_HEADER_LEN + datalen;

	/* udp checksum */
	fill_checksum(UDP_CHECKSUM_H_P, IP_SRC_P, 16 + datalen, 1);

	enc28j60_packetSend(ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN + datalen);
}
