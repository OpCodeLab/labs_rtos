#ifndef TEMP_H
#define TEMP_H

#include <stdbool.h>
#include <stddef.h>

#define TEMP_ALARM_C 25

/* Limit t to [min, max]. */
int temp_clamp(int t, int min, int max);

/* Average of n samples. Returns 0 when n == 0. */
int temp_average(const int *samples, size_t n);

/* Sum of the n values in buff. */
int temp_crc(int *buff, int n);

/* true when t is above TEMP_ALARM_C. */
bool temp_is_alarm(int t);

#endif
