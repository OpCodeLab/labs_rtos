/*
 * Lab 14: the code under test.
 * Plain C, no kernel calls: easy to test on the PC (native_sim).
 */

 #include "temp.h"


	

int temp_clamp(int t, int min, int max)
{
	if (t < min) {
		return min;
	}
	if (t > max) {
		return max;
	}
	return t;
}

int temp_average(const int *samples, size_t n)
{
	int sum = 0;

	if (n == 0) {
		return 0;
	}
	for (size_t i = 0; i < n; i++) {
		sum += samples[i];
	}
	return sum / (int)n;
}

bool temp_is_alarm(int t)
{
	return t > TEMP_ALARM_C;
}
