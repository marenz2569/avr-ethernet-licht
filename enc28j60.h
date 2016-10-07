#ifndef ENC28J60_H__
#define ENC28J60_H__

#include <stdint.h>

#include "enc28j60_config.h"

uint16_t enc28j60_curPacketPointer;

uint8_t enc28j60_buffer[ENC28J60_BUFFERSIZE];

uint8_t enc28j60_readOp(uint8_t op, uint8_t addr);

void enc28j60_writeOp(uint8_t op, uint8_t adrr, uint8_t data);

void enc28j60_readBuf(uint16_t len, uint8_t *data);

void enc28j60_writeBuf(uint16_t len, const uint8_t *data);

void enc28j60_setBank(uint8_t addr);

uint8_t enc28j60_readReg(uint8_t addr);

uint16_t enc28j60_readReg16(uint8_t addr);

void enc28j60_writeReg(uint8_t addr, uint8_t data);

void enc28j60_writeReg16(uint8_t addr, uint16_t data);

uint16_t enc28j60_readPhy(uint8_t addr);

void enc28j60_writePhy(uint8_t addr, uint16_t data);

void enc28j60_reset(void);

uint8_t enc28j60_init(const uint8_t *macaddr);

uint16_t enc28j60_packetReceive(void);

void enc28j60_freePacketSpace(void);

void enc28j60_packetSend(uint16_t len);

void enc28j60_dma(uint16_t start, uint16_t end, uint16_t dest);

#endif
