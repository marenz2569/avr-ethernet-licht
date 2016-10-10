#ifndef WS2812_H__
#define WS2812_H__

#include <stdint.h>

#define clamp_to_0_1(x)         (x>0?(x<1?x:1):0)
#define clamp_to_0_255(x)       (x>0?(x<255?x:255):0)

typedef struct {
	uint8_t g,
		r,
		b;
} rgb;

volatile uint8_t ws2812_locked;

void ws2812_sync();

uint8_t ws2812_set_rgb_at(uint16_t index, const rgb t);

rgb hsi2rgb(float h_, float s_, float i_);

#endif
