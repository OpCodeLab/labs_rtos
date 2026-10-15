/*
 * OpCode Labs - Zephyr training
 * Lab 02: button interrupt -> semaphore -> pulse width -> message queue.
 *
 * Port of freertos_lab/main6.c (MAIN6).
 *
 *   Task1 "Periodic"   : toggles LED0 every second
 *   Task2 "APeriodic"  : wakes on the button semaphore, measures how long the
 *                        button stays pressed, sends the width to Queue1
 *   Task3 "Processing" : reads Queue1, prints "Short" or "Long" button
 *
 *   FreeRTOS                               Zephyr
 *   -------------------------------------  ----------------------------------------
 *   xSemaphoreCreateBinary()               K_SEM_DEFINE(sem1, 0, 1)
 *   xQueueCreate(10, sizeof(uint32_t))     K_MSGQ_DEFINE(queue1, sizeof(uint32_t), 10, 4)
 *   HAL_GPIO_EXTI_Callback (EXTI0 ISR)     gpio_init_callback() + gpio_add_callback()
 *   xSemaphoreGiveFromISR + YIELD_FROM_ISR k_sem_give()  (reschedule is automatic)
 *   xSemaphoreTake(Sem1, portMAX_DELAY)    k_sem_take(&sem1, K_FOREVER)
 *   xTaskGetTickCount()                    k_uptime_get_32()  (milliseconds)
 *   xQueueSend(Queue1, &w, 100)            k_msgq_put(&queue1, &w, K_MSEC(100))
 *   xQueueReceive(Queue1, &w, MAX_DELAY)   k_msgq_get(&queue1, &w, K_FOREVER)
 *   HAL_GPIO_ReadPin(GPIOA, PIN_0)         gpio_pin_get_dt(&button)
 *
 * The button is the devicetree alias sw0 (PA0 on stm32f4_disco, PC13 on the
 * Nucleo boards). gpio_pin_get_dt() returns the *logical* level, so the
 * active-low Nucleo button works with the same code.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE    1024

/* zephyr_prio = 10 - freertos_prio */
#define PRIO_PERIODIC   8   /* FreeRTOS 2 */
#define PRIO_APERIODIC  7   /* FreeRTOS 3 */
#define PRIO_PROCESSING 7   /* FreeRTOS 3 */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

K_SEM_DEFINE(sem1, 0, 1);                          /* binary semaphore */
K_MSGQ_DEFINE(queue1, sizeof(uint32_t), 10, 4);    /* 10 x uint32_t    */

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
		k_sem_take(&sem1, K_FOREVER);
		/* Take the first tick */
		first_tick = k_uptime_get_32();
		/* While button still pressed ... (busy wait, like the FreeRTOS lab:
		 * lower priority threads are starved while the button is held) */
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
		/* Blocked until the queue receives a new value */
		k_msgq_get(&queue1, &pulse_width, K_FOREVER);
		if ((pulse_width <= 400) && (pulse_width > 100)) {
			printk("Short Button detected ...\n");
		} else if (pulse_width > 400) {
			printk("Long Button detected ...\n");
		}
	}
}

/* Button ISR (FreeRTOS: HAL_GPIO_EXTI_Callback).
 * No portYIELD_FROM_ISR(): Zephyr reschedules by itself on ISR exit if a
 * higher priority thread became ready. */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	k_sem_give(&sem1);
}

int main(void)
{
	printk("LAB6 (Zephyr) ------------------- \n");

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		printk("GPIO not ready\n");
		return 0;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	/* FreeRTOS: GPIO_MODE_IT_RISING + HAL_NVIC_EnableIRQ(EXTI0_IRQn) */
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
