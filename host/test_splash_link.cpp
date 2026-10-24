/* Compile the fitter as C and call it from C++, the way the sketch does. */
#include "splash_fit.h"

#include <stdlib.h>

int main()
{
    uint16_t *dst = (uint16_t *)calloc((size_t)SPLASH_PANEL * SPLASH_PANEL,
                                       sizeof(uint16_t));
    splash_fit_t fit;
    const uint8_t red[4] = {255, 0, 0, 255};
    if (!dst) return 1;
    if (!splash_fit_begin(&fit, dst, SPLASH_PANEL, SPLASH_PANEL)) return 1;
    splash_fit_pixels(&fit, 233, 233, 1, 1, red);
    splash_fit_mask(&fit);
    int rc = dst[233 * SPLASH_PANEL + 233] == 0xF800 ? 0 : 1;
    free(dst);
    return rc;
}
