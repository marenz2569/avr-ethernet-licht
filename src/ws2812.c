#include <avr/interrupt.h>
#include <math.h>

#include "ws2812.h"
#include "ws2812_config.h"
#include "cos_approx.h"

rgb ws2812_buf[WS2812_LEDS];
rgb *ws2812_buffer = ws2812_buf;
const size_t ws2812_led = WS2812_LEDS;
const size_t *ws2812_leds = &ws2812_led;

uint8_t ws2812_set_rgb_at(const uint16_t index, const rgb * const t)
{
	if (index < WS2812_LEDS && !ws2812_locked) {
		memcpy(ws2812_buffer + index, t, 3);

		return 1;
	}
	return 0;
}

rgb hsi2rgb(float h_, float s_, float i_)
{
	h_ = fmod(h_, 360);
	/* convert h to radiants */
	h_ = M_PI * h_ / 180.0;
	/* clamp s and i to interval [0:1] */
	s_ = clamp_to_0_1(s_);
	i_ = clamp_to_0_1(i_);

	rgb ret;

	if (s_ == 0.0) {
		ret.r = ret.g = ret.b = i_ * 255;
	} else {
		float r_, g_, b_;
#define hsi2rgb_color1 ((1 - s_) / 3)
#define hsi2rgb_color2 ((1 + s_ * cos_approx(h_) / cos_approx((M_PI / 3.0) - h_)) / 3)
		if ((h_ >= 0.0) && (h_ < 2.0 * M_PI / 3.0)) {
			b_ = hsi2rgb_color1;
			r_ = hsi2rgb_color2;
			g_ = 1 - b_ - r_;
		} else if ((h_ >= 2.0 * M_PI / 3.0) && (h_ < 4.0 * M_PI / 3.0)) {
			h_ -= 2.0 * M_PI / 3.0;
			r_ = hsi2rgb_color1;
			g_ = hsi2rgb_color2;
			b_ = 1 - r_ - g_;
		} else {
			h_ -= 4.0 * M_PI / 3.0;
			g_ = hsi2rgb_color1;
			b_ = hsi2rgb_color2;
			r_ = 1 - g_ - b_;
		}

		/* removed faktor 3 */
		r_ =  i_ * r_ * 255;
		g_ =  i_ * g_ * 255;
		b_ =  i_ * b_ * 255;

		/* clamp r, g and b to interval [0:255] */
		ret.r = clamp_to_0_255(r_);
		ret.g = clamp_to_0_255(g_);
		ret.b = clamp_to_0_255(b_);
	}

	return ret;
}
