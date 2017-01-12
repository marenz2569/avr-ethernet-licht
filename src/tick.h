#ifndef TICK_H__
#define TICK_H__

#include <inttypes.h>

#define async_set(t) \
	async_delay = systick + t

#define async_wait() \
	while (systick < async_delay)

volatile uint64_t systick;
uint64_t async_delay;

void tick_init(void);

#endif
