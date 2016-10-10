#include <math.h>

#include "cos_approx.h"

/* cosin approximation derived from http://lab.polygonal.de/2007/07/18/fast-and-accurate-sinecosine-approximation/ */
float cos_approx(float x)
{
	x += M_PI_2;

	while (x < - M_PI)
		x += 2 * M_PI;

	while (x > M_PI)
		x -= 2 * M_PI;

	float cos;

	if (x < 0)
		cos = 1.27323954 * x + 0.405284735 * x * x;
	else
		cos = 1.27323954 * x - 0.405284735 * x * x;

	if (cos < 0)
		cos = .225 * (cos *-cos - cos) + cos;
	else
		cos = .225 * (cos * cos - cos) + cos;

	return cos;
}
