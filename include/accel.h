#ifndef PI_HOUR_ACCEL_H
#define PI_HOUR_ACCEL_H

#include <stdbool.h>

bool accel_init(void);

// Acceleration in g (approx), sensor frame
bool accel_read_g(float *ax, float *ay, float *az);

#endif
