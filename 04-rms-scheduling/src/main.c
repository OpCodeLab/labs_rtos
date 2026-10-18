/*
 * OpCode Labs - Zephyr training
 * Lab 04: Rate Monotonic Scheduling (RMS) - 4 periodic tasks with a CPU load.
 *
 * Port of freertos_lab/main_rms.c (RMS).
 *
 *   Task  Period Ti  Load Ci  FreeRTOS prio  Zephyr prio  LED
 *   T1    1000 ms    200 ms   2              8            led0 (PD12)
 *   T2     150 ms     50 ms   7              3            led1 (PD13)
 *   T3     350 ms     80 ms   5              5            led2 (PD14)
 *   T4     350 ms    100 ms   5              5            led3 (PD15)
 *
 *   U = 200/1000 + 50/150 + 80/350 + 100/350 = 0.85
 *   RMS bound for n=4 : 4 * (2^(1/4) - 1) = 0.757  -> the bound test fails,
 *   watch the LEDs / a logic analyser to see whether deadlines are met.
 *
 *   FreeRTOS                                  Zephyr
 *   ----------------------------------------  ----------------------------------
 *   xLastWakeTime = xTaskGetTickCount()       next = k_uptime_get()
 *   vTaskDelayUntil(&xLastWakeTime, T)        next += T;
 *                                             k_sleep(K_TIMEOUT_ABS_MS(next))
 *   HAL_Delay(1)  (busy wait)                 k_busy_wait(1000)  (microseconds)
 *   configUSE_TIME_SLICING (default 1)        CONFIG_TIMESLICING + TIMESLICE_SIZE
 *   4 task functions                          1 function, parameters in p1..p3
 *
 * Priorities: zephyr_prio = 10 - freertos_prio (smaller = more urgent).
 * T3 and T4 share a priority: prj.conf enables a 1 ms time slice so they
 * round-robin like FreeRTOS does.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE   1024

#define T1_PERIODE   1000
#define T2_PERIODE   150
#define T3_PERIODE   350
#define T4_PERIODE   350

#define T1_PRIO      8   /* FreeRTOS 2 */
#define T2_PRIO      3   /* FreeRTOS 7 */
#define T3_PRIO      5   /* FreeRTOS 5 */
#define T4_PRIO      5   /* FreeRTOS 5 */

#define LED_SPEC(alias) GPIO_DT_SPEC_GET_OR(DT_ALIAS(alias), gpios, {0})

static const struct gpio_dt_spec leds[4] = {
	LED_SPEC(led0), LED_SPEC(led1), LED_SPEC(led2), LED_SPEC(led3),
};

struct rms_task {
	const char *name;
	uint32_t period_ms;   /* Ti */
	uint32_t load_ms;     /* Ci */
	int prio;
	int led;
};

static const struct rms_task tasks[] = {
	{ "T1", T1_PERIODE, 200, T1_PRIO, 0 },
	{ "T2", T2_PERIODE,  50, T2_PRIO, 1 },
	{ "T3", T3_PERIODE,  80, T3_PRIO, 2 },
	{ "T4", T4_PERIODE, 100, T4_PRIO, 3 },
};

K_THREAD_STACK_ARRAY_DEFINE(stacks, ARRAY_SIZE(tasks), STACK_SIZE);
static struct k_thread threads[ARRAY_SIZE(tasks)];

/* Burn CPU for time_ms while toggling the LED every ms (FreeRTOS: LoadFunction).
 * Like HAL_Delay() it measures wall time: if the task is preempted, the
 * preemption time is counted in the load. */
static void load_function(uint32_t time_ms, int led)
{
	for (uint32_t i = 0; i < time_ms / 2; i++) {
		k_busy_wait(1000);
		if (leds[led].port != NULL) {
			gpio_pin_toggle_dt(&leds[led]);
		}
	}
}

/* Periodic task: p1 = its descriptor */
static void periodic_task(void *p1, void *p2, void *p3)
{
	const struct rms_task *t = p1;
	int64_t next_wake = k_uptime_get();

	while (1) {
		load_function(t->load_ms, t->led);

		/* vTaskDelayUntil(): absolute wake-up time, no drift */
		next_wake += t->period_ms;
		k_sleep(K_TIMEOUT_ABS_MS(next_wake));
	}
}
/* Static threads: stack, thread object and parameters are all defined at
 * build time. SYS_FOREVER_MS = created but not started; main() starts them
 * once the LEDs are configured. */
K_THREAD_DEFINE(t1, STACK_SIZE, periodic_task, (void *)&tasks[0], NULL, NULL,
		T1_PRIO, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(t2, STACK_SIZE, periodic_task, (void *)&tasks[1], NULL, NULL,
		T2_PRIO, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(t3, STACK_SIZE, periodic_task, (void *)&tasks[2], NULL, NULL,
		T3_PRIO, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(t4, STACK_SIZE, periodic_task, (void *)&tasks[3], NULL, NULL,
		T4_PRIO, 0, SYS_FOREVER_MS);

int main(void)
{
	printk("LAB RMS (Zephyr) ------------------- \n");

	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (leds[i].port != NULL && gpio_is_ready_dt(&leds[i])) {
			gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		}
	}

	k_thread_start(t1);
	k_thread_start(t2);
	k_thread_start(t3);
	k_thread_start(t4);


	return 0;
}
