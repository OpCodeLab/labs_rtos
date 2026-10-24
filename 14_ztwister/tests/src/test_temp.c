/*
 * Lab 14: unit tests for temp.c with ztest.
 * Each ZTEST() is one test case. Twister builds, runs and reports them.
 */
#include <zephyr/ztest.h>
#include "temp.h"

ZTEST(temp, test_clamp_inside)
{
	zassert_equal(temp_clamp(20, 0, 50), 20);
}

ZTEST(temp, test_clamp_outside)
{
	zassert_equal(temp_clamp(-10, 0, 50), 0, "below min must return min");
	zassert_equal(temp_clamp(99, 0, 50), 50, "above max must return max");
}

ZTEST(temp, test_average)
{
	int samples[] = {20, 22, 24};

	zassert_equal(temp_average(samples, ARRAY_SIZE(samples)), 22);
}

ZTEST(temp, test_average_empty)
{
	/* Edge case: no samples must not divide by 0. */
	zassert_equal(temp_average(NULL, 0), 0);
}





ZTEST(temp, test_alarm)
{
	zassert_false(temp_is_alarm(TEMP_ALARM_C), "25 C is not an alarm");
	zassert_true(temp_is_alarm(TEMP_ALARM_C + 1));
}

/* One suite named "temp", no setup/teardown. */
ZTEST_SUITE(temp, NULL, NULL, NULL, NULL, NULL);
