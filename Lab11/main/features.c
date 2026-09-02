#include <math.h>
#include "features.h"

void compute_features(const float *win, int n, float *rms_out, float *peak_out)
{
    float mean = 0;
    for (int i = 0; i < n; i++) {
        mean += win[i];
    }
    mean /= n;

    float ss = 0, peak = 0;
    for (int i = 0; i < n; i++) {
        float d = win[i] - mean;
        ss += d * d;
        if (fabsf(d) > peak) {
            peak = fabsf(d);
        }
    }

    *rms_out = sqrtf(ss / n);
    *peak_out = peak;
}