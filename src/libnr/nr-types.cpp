#include <libnr/nr-types.h>
#include <glib.h>
#include <math.h>


/** Scales this vector to make it a unit vector (within rounding error).
 *
 *  Requires: *this != (0, 0).
 *	      Neither coordinate is NaN.
 *  Ensures: L2(*this) very near 1.0.
 *
 *  The current version tries to handle infinite coordinates gracefully,
 *  but it's not clear that any callers need that.
 */
void NR::Point::normalize() {
	double len = hypot(_pt[0], _pt[1]);
	g_return_if_fail(len != 0);
	g_return_if_fail(!isnan(len));
	static double const inf = 1e400;
	if(len != inf) {
		*this /= len;
	} else {
		unsigned n_inf_coords = 0;
		/* Delay updating pt in case neither coord is infinite. */
		NR::Point tmp;
		for ( unsigned i = 0 ; i < 2 ; ++i ) {
			if ( _pt[i] == inf ) {
				++n_inf_coords;
				tmp[i] = 1.0;
			} else if ( _pt[i] == -inf ) {
				++n_inf_coords;
				tmp[i] = -1.0;
			} else {
				tmp[i] = 0.0;
			}
		}
		switch (n_inf_coords) {
		case 0:
			/* Can happen if both coords are near +/-DBL_MAX. */
			*this /= 4.0;
			len = hypot(_pt[0], _pt[1]);
			g_assert(len != inf);
			*this /= len;
			break;

		case 1:
			*this = tmp;
			break;

		case 2:
			*this = sqrt(0.5) * tmp;
			break;
		}
	}
}
