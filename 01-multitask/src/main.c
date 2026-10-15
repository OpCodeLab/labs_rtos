/*
 * OpCode Labs - Zephyr training
 * Lab 01: four periodic threads, each one toggles its own LED.
 *
 * Port of freertos_lab/main.c (MAIN1).
 *
 *   FreeRTOS                          Zephyr
 *   --------------------------------  -----------------------------------------
 *   HAL_Init / MX_GPIO_Init           devicetree + gpio_pin_configure_dt()
 *   xTaskCreate(Task1,"T1",128,..,2)  k_thread_create(&t1, stack1, ..., prio)
 *   vTaskStartScheduler()             nothing: the kernel already runs main()
 *   vTaskDelay(1000)                  k_msleep(1000)
 *   HAL_GPIO_TogglePin(GPIOD, PD12)   gpio_pin_toggle_dt(&leds[0])
 *   printf                            printk
 *
 * LEDs come from the devicetree aliases led0..led3. On stm32f4_disco they are
 * PD12 green, PD13 orange, PD14 red, PD15 blue (same pins as the FreeRTOS lab).
 * Nucleo boards only have led0: the missing LEDs are skipped.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE   1024

/* FreeRTOS: bigger number = higher priority.  Zephyr: smaller number = higher.
 * Rule used in all labs: zephyr_prio = 10 - freertos_prio  (FreeRTOS 2 -> 8). */
#define TASK_PRIO    8

#define LED_SPEC(alias) GPIO_DT_SPEC_GET_OR(DT_ALIAS(alias), gpios, {0})

static const struct gpio_dt_spec leds[4] = {
	LED_SPEC(led0), LED_SPEC(led1), LED_SPEC(led2), LED_SPEC(led3),
};

/* One stack + one thread object per task (FreeRTOS allocates them in the heap) */
K_THREAD_STACK_DEFINE(stack1, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack2, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack3, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack4, STACK_SIZE);
static struct k_thread thread1, thread2, thread3, thread4;

static void led_toggle(int i)
{
	if (leds[i].port != NULL) {
		gpio_pin_toggle_dt(&leds[i]);
	}
}

static void leds_init(void)
{
	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (leds[i].port != NULL && gpio_is_ready_dt(&leds[i])) {
			gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		}
	}
}

/* Periodic task */
static void task1(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 1 is running....\n");
		led_toggle(0);
		k_msleep(1000);
	}
}

/* Periodic task */
static void task2(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 2 is running....\n");
		led_toggle(1);
		k_msleep(500);
	}
}

/* Periodic task */
static void task3(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 3 is running....\n");
		led_toggle(2);
		k_msleep(300);
	}
}

/* Periodic task */
static void task4(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 4 is running....\n");
		led_toggle(3);
		k_msleep(700);
	}
}

int main(void)
{
	printk("LAB RTOS (Zephyr) ------------------- \n");

	leds_init();

	/* K_NO_WAIT = start now. main() has priority 0 (highest), so the new
	 * threads only run once main() returns. */
	k_thread_create(&thread1, stack1, STACK_SIZE, task1, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread1, "T1");
	k_thread_create(&thread2, stack2, STACK_SIZE, task2, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread2, "T2");
	k_thread_create(&thread3, stack3, STACK_SIZE, task3, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread3, "T3");
	k_thread_create(&thread4, stack4, STACK_SIZE, task4, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread4, "T4");

	/* No vTaskStartScheduler(): returning from main() is allowed in Zephyr. */
	return 0;
}
