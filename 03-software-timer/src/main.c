/*
 * OpCode Labs - Zephyr training
 * Lab 03: Lab 02 + button debounce + software timer.
 *
 * Port of freertos_lab/main10.c (MAIN10).
 *
 *   short press (100..400 ms) -> start the 100 ms auto-reload timer
 *   long press  (> 400 ms)    -> stop the timer
 *
 *   FreeRTOS                                 Zephyr
 *   ---------------------------------------  -----------------------------------
 *   xTimerCreate("STimer",100,pdTRUE,0,cb)   K_TIMER_DEFINE(stimer, cb, NULL)
 *   xTimerStart(sTim, 0)                     k_timer_start(&stimer, K_MSEC(100),
 *                                                          K_MSEC(100))
 *   xTimerStop(sTim, 0)                      k_timer_stop(&stimer)
 *   vTimerCallback(TimerHandle_t)            stimer_expiry(struct k_timer *)
 *   vTaskDelay(DEBOUNCE_MS)                  k_msleep(DEBOUNCE_MS)
 *   xSemaphoreTake(Sem1, 0)                  k_sem_take(&sem1, K_NO_WAIT)
 *
 * IMPORTANT difference:
 *   FreeRTOS timer callbacks run in the "timer service" task (Tmr Svc).
 *   Zephyr k_timer expiry functions run in INTERRUPT context (system clock ISR):
 *   keep them short, never block. To get FreeRTOS-like behaviour, submit a
 *   k_work item from the expiry function and do the job in the work handler
 *   (system work queue thread). Set USE_WORKQUEUE to 1 to try it.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define USE_WORKQUEUE  0

#define STACK_SIZE     1024
#define DEBOUNCE_MS    30u
#define TIMER_PERIOD   K_MSEC(100)

/* zephyr_prio = 10 - freertos_prio */
#define PRIO_PERIODIC   8   /* FreeRTOS 2 */
#define PRIO_APERIODIC  7   /* FreeRTOS 3 */
#define PRIO_PROCESSING 7   /* FreeRTOS 3 */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

K_SEM_DEFINE(sem1, 0, 1);
K_MSGQ_DEFINE(queue1, sizeof(uint32_t), 10, 4);

#if USE_WORKQUEUE
static void timer_work_handler(struct k_work *work)
{
	/* Thread context (system work queue): blocking calls are allowed here */
	printk("Timer is Running (work queue)\n");
}
K_WORK_DEFINE(timer_work, timer_work_handler);
#endif

/* Timer callback - runs in ISR context in Zephyr */
static void stimer_expiry(struct k_timer *timer)
{
#if USE_WORKQUEUE
	k_work_submit(&timer_work);
#else
	printk("Timer is Running\n");
#endif
}

K_TIMER_DEFINE(stimer, stimer_expiry, NULL);

K_THREAD_STACK_DEFINE(stack1, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack2, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack3, STACK_SIZE);
static struct k_thread thread1, thread2, thread3;

/* Periodic task */
static void task1(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 1 is running....\n");
		gpio_pin_toggle_dt(&led);
		k_msleep(1000);
	}
}

/* Aperiodic task: released by the button ISR */
static void task2(void *p1, void *p2, void *p3)
{
	uint32_t first_tick, second_tick, pulse_width;

	while (1) {
		/* Debounce filter */
		k_sem_take(&sem1, K_FOREVER);
		k_msleep(DEBOUNCE_MS);              /* let the contacts settle   */
		(void)k_sem_take(&sem1, K_NO_WAIT); /* drop the bounce edges     */

		/* Take the first tick */
		first_tick = k_uptime_get_32();
		/* While button still pressed ... */
		while (gpio_pin_get_dt(&button) == 1) {
		}
		second_tick = k_uptime_get_32();
		pulse_width = second_tick - first_tick;
		k_msgq_put(&queue1, &pulse_width, K_MSEC(100));
	}
}

/* Processing task */
static void task3(void *p1, void *p2, void *p3)
{
	uint32_t pulse_width;

	while (1) {
		k_msgq_get(&queue1, &pulse_width, K_FOREVER);
		if ((pulse_width <= 400) && (pulse_width > 100)) {
			printk("Short Button detected ...\n");
			/* Start timer (auto-reload: initial delay + period) */
			k_timer_start(&stimer, TIMER_PERIOD, TIMER_PERIOD);
		} else if (pulse_width > 400) {
			printk("Long Button detected ...\n");
			/* Stop timer */
			k_timer_stop(&stimer);
		}
	}
}

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	k_sem_give(&sem1);
}

int main(void)
{
	printk("LAB10 (Zephyr) ------------------- \n");

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		printk("GPIO not ready\n");
		return 0;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	k_thread_create(&thread1, stack1, STACK_SIZE, task1, NULL, NULL, NULL,
			PRIO_PERIODIC, 0, K_NO_WAIT);
	k_thread_name_set(&thread1, "Periodic");
	k_thread_create(&thread2, stack2, STACK_SIZE, task2, NULL, NULL, NULL,
			PRIO_APERIODIC, 0, K_NO_WAIT);
	k_thread_name_set(&thread2, "APeriodic");
	k_thread_create(&thread3, stack3, STACK_SIZE, task3, NULL, NULL, NULL,
			PRIO_PROCESSING, 0, K_NO_WAIT);
	k_thread_name_set(&thread3, "Processing");

	return 0;
}
