#include <avr/interrupt.h>
#include <util/delay.h>

#include "spi.h"
#include "enc28j60.h"
#include "enc28j60_defs.h"
#include "enc28j60_config.h"

static uint8_t enc28j60_bank;

static uint16_t enc28j60_nextPacketPointer = ENC28J60_RX_START;

const uint8_t mac[] = MAC;

uint8_t enc28j60_buf[ENC28J60_BUFFERSIZE];
uint8_t *enc28j60_buffer = enc28j60_buf;

uint8_t enc28j60_readOp(uint8_t op, uint8_t addr)
{
	uint8_t sreg = SREG,
		res;

	cli();
	ENC28J60_enable;

	spi_wrrd(op | (addr & ADDR_MASK));
	if (addr & 0x80)
		spi_wrrd(0x00);
	res = spi_wrrd(0x00);
	
	ENC28J60_disable;
	SREG = sreg;

	return res;
}

void enc28j60_writeOp(uint8_t op, uint8_t addr, uint8_t data)
{
	uint8_t sreg = SREG;

	cli();
	ENC28J60_enable;

	spi_wrrd(op | (addr & ADDR_MASK));
	spi_wrrd(data);

	ENC28J60_disable;
	SREG = sreg;
}

void enc28j60_readBuf(uint16_t len, uint8_t *data)
{
	uint8_t sreg = SREG;

	cli();
	ENC28J60_enable;

	spi_wrrd(ENC28J60_READ_BUF_MEM);

	do {
		*data++ = spi_wrrd(0x00);
	} while (--len);

	ENC28J60_disable;
	SREG = sreg;
}

void enc28j60_writeBuf(uint16_t len, const uint8_t *data)
{
	uint8_t sreg = SREG;

	cli();
	ENC28J60_enable;

	spi_wrrd(ENC28J60_WRITE_BUF_MEM);

	do
		spi_wrrd(*data++);
	while (--len);

	ENC28J60_disable;
	SREG = sreg;
}

void enc28j60_set_random_access(uint16_t offset)
{
	uint16_t j;

	j = (enc28j60_curPacketPointer + offset) % (ENC28J60_RX_END + 1);
	j += ENC28J60_RX_START%(ENC28J60_RX_START + (j % (enc28j60_curPacketPointer + offset)));

	enc28j60_writeReg16(ERDPTL, j);
}

void enc28j60_setBank(uint8_t addr)
{
	if ((addr & BANK_MASK) != enc28j60_bank) {
		enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_BSEL1 | ECON1_BSEL0);
		enc28j60_bank = addr & BANK_MASK;
		enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, enc28j60_bank >> 5);
	}
}

uint8_t enc28j60_readReg(uint8_t addr)
{
	enc28j60_setBank(addr);
	return enc28j60_readOp(ENC28J60_READ_CTRL_REG, addr);
}

uint16_t enc28j60_readReg16(uint8_t addr)
{
	return (enc28j60_readReg(addr) + (enc28j60_readReg(addr+1) << 8));
}

void enc28j60_writeReg(uint8_t addr, uint8_t data)
{
	enc28j60_setBank(addr);
	enc28j60_writeOp(ENC28J60_WRITE_CTRL_REG, addr, data);
}

void enc28j60_writeReg16(uint8_t addr, uint16_t data)
{
	enc28j60_writeReg(addr, data);
	enc28j60_writeReg(addr + 1, data >> 8);
}

uint16_t enc28j60_readPhy(uint8_t addr)
{
	enc28j60_writeReg(MIREGADR, addr);
	enc28j60_writeReg(MICMD, MICMD_MIIRD);
	while (enc28j60_readReg(MISTAT) & MISTAT_BUSY)
		;
	enc28j60_writeReg(MICMD, 0x00);
	return enc28j60_readReg(MIRDL + 1);
}

void enc28j60_writePhy(uint8_t addr, uint16_t data)
{
	enc28j60_writeReg(MIREGADR, addr);
	enc28j60_writeReg16(MIWRL, data);
	while (enc28j60_readReg(MISTAT) & MISTAT_BUSY)
		;
}

void enc28j60_reset(void)
{
	uint8_t sreg = SREG;

	cli();
	ENC28J60_enable;

	spi_wrrd(ENC28J60_SOFT_RESET);

	ENC28J60_disable;
	SREG = sreg;
}

uint8_t enc28j60_init(void)
{
	if (!(SPCR & _BV(SPE)))
		spi_init();

	ENC28J60_CS_DDR |= _BV(ENC28J60_CS_PIN);
	ENC28J60_disable;

	enc28j60_reset();
	_delay_ms(2);
	while (!enc28j60_readOp(ENC28J60_READ_CTRL_REG, ESTAT) & ESTAT_CLKRDY)
		;

	/* configure usage of buffer by rx and tx */
	enc28j60_curPacketPointer = enc28j60_nextPacketPointer;
	enc28j60_nextPacketPointer = ENC28J60_RX_START;
	enc28j60_writeReg16(ERXSTL, ENC28J60_RX_START);
	enc28j60_writeReg16(ERXRDPTL, ENC28J60_RX_START);
	enc28j60_writeReg16(ERXNDL, ENC28J60_RX_END);
	enc28j60_writeReg16(ETXSTL, ENC28J60_TX_START);
	enc28j60_writeReg16(ETXNDL, ENC28J60_TX_END);

	/* filter based on OR logic and unicast, broadcast and valid crc */
	enc28j60_writeReg(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);

	/* enable interrupts and packet pending interrupt */
	enc28j60_writeReg(EIE, EIE_INTIE | EIE_PKTIE);

	enc28j60_writeReg(MACON1, MACON1_MARXEN|MACON1_TXPAUS|MACON1_RXPAUS);
	enc28j60_writeReg(MACON2, 0x00);
	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, MACON3, MACON3_PADCFG0|MACON3_TXCRCEN|MACON3_FRMLNEN);
	enc28j60_writeReg16(MAIPGL, 0x0C12);
	enc28j60_writeReg(MABBIPG, 0x12);
	enc28j60_writeReg16(MAMXFLL, ENC28J60_MAX_FRAMELEN);
	enc28j60_writeReg(MAADR5, mac[0]);
	enc28j60_writeReg(MAADR4, mac[1]);
	enc28j60_writeReg(MAADR3, mac[2]);
	enc28j60_writeReg(MAADR2, mac[3]);
	enc28j60_writeReg(MAADR1, mac[4]);
	enc28j60_writeReg(MAADR0, mac[5]);
	enc28j60_writePhy(PHCON2, PHCON2_HDLDIS);

	/* enable packet reception of packets */
	enc28j60_setBank(ECON1);
	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);

	return enc28j60_readReg(EREVID);
}

uint16_t enc28j60_packetReceive(void)
{
	uint16_t len = 0;

	if (enc28j60_readReg(EPKTCNT)) {
		/* set read pointer */
		enc28j60_writeReg16(ERDPTL, enc28j60_nextPacketPointer);

		enc28j60_curPacketPointer = enc28j60_nextPacketPointer;

		struct {
			uint16_t nextPacket;
			uint16_t length;
			uint16_t status;
		} header;

		enc28j60_readBuf(sizeof(header), (uint8_t *) &header);

		len = header.length - 4;
		if (len > ENC28J60_BUFFERSIZE)
			len = ENC28J60_BUFFERSIZE;

		enc28j60_readBuf(len, enc28j60_buffer);

		enc28j60_nextPacketPointer = header.nextPacket;
	}
	return len;
}

void enc28j60_freePacketSpace(void)
{
	if (enc28j60_nextPacketPointer == ENC28J60_RX_START)
		enc28j60_writeReg16(ERXRDPTL, ENC28J60_RX_END);
	else
		enc28j60_writeReg16(ERXRDPTL, enc28j60_nextPacketPointer - 1);

	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
}

void enc28j60_packetSend(uint16_t len) {
	uint8_t retry = 0;

	while (1) {
		// latest errata sheet: DS80349C
		// always reset transmit logic (Errata Issue 12)
		// the Microchip TCP/IP stack implementation used to first check
		// whether TXERIF is set and only then reset the transmit logic
		// but this has been changed in later versions; possibly they
		// have a reason for this; they don't mention this in the errata
		// sheet
		enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRST);
		enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
		enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF|EIR_TXIF);

		// prepare new transmission
		if (retry == 0) {
			enc28j60_writeReg16(EWRPTL, ENC28J60_TX_START);
			enc28j60_writeReg16(ETXNDL, ENC28J60_TX_START+len);
			enc28j60_writeOp(ENC28J60_WRITE_BUF_MEM, 0, 0x00);
			enc28j60_writeBuf(len, enc28j60_buffer);
		}

		// initiate transmission
		enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

		// wait until transmission has finished; referrring to the data sheet and
		// to the errata (Errata Issue 13; Example 1) you only need to wait until either
		// TXIF or TXERIF gets set; however this leads to hangs; apparently Microchip
		// realized this and in later implementations of their tcp/ip stack they introduced
		// a counter to avoid hangs; of course they didn't update the errata sheet
		uint16_t count = 1000;
		while ((enc28j60_readReg(EIR) & (EIR_TXIF | EIR_TXERIF)) == 0 && --count > 0)
			;

		if (!(enc28j60_readReg(EIR) & EIR_TXERIF) && count > 0) {
			// no error; start new transmission
			break;
		}

		// cancel previous transmission if stuck
		enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRTS);

		// Check whether the chip thinks that a late collision ocurred; the chip
		// may be wrong (Errata Issue 13); therefore we retry. We could check
		// LATECOL in the ESTAT register in order to find out whether the chip
		// thinks a late collision ocurred but (Errata Issue 15) tells us that
		// this is not working. Therefore we check TSV
		struct {
			uint8_t bytes[7];
		} tsv;
		uint16_t etxnd = enc28j60_readReg16(ETXNDL);
		enc28j60_writeReg16(ERDPTL, etxnd+1);
		enc28j60_readBuf(sizeof(tsv), (uint8_t *) &tsv);
		// LATECOL is bit number 29 in TSV (starting from 0)

		if (!((enc28j60_readReg(EIR) & EIR_TXERIF) && (tsv.bytes[3] & 1<<5) /*tsv.transmitLateCollision*/) || retry
> 16U) {
			// there was some error but no LATECOL so we do not repeat
			break;
		}

		retry++;
	}
}

void enc28j60_dma(uint16_t start, uint16_t end, uint16_t dest)
{
	enc28j60_writeReg16(EDMASTL, start);
	enc28j60_writeReg16(EDMANDL, end);
	enc28j60_writeReg16(EDMADSTL, dest);

	enc28j60_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_CSUMEN);

	enc28j60_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_DMAST);

	while (enc28j60_readReg(ECON1) & ECON1_DMAST)
		;
}
