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
 *
 * Shell (CONFIG_SHELL=y), on the console UART:
 *   led on|off|toggle <n>   drive LED n by hand (its task stops blinking it)
 *   led blink <n>           give LED n back to its task
 *   led status              state of the 4 LEDs
 *   led log on|off          show/hide the "Task x is running" messages
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>

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

static struct k_thread thread1, thread2, thread3, thread4;

/* Bit i set = LED i is driven by the shell, its task leaves it alone.
 * atomic_t: written by the shell thread, read by the 4 tasks. */
static atomic_t manual_mask = ATOMIC_INIT(0);

/* Task messages are off by default, otherwise they flood the shell prompt */
static atomic_t log_on = ATOMIC_INIT(0);

#define TASK_LOG(...) do { if (atomic_get(&log_on)) printk(__VA_ARGS__); } while (0)

/* Called by the tasks: does nothing while the shell owns the LED */
static void led_toggle(int i)
{
	if (leds[i].port != NULL && !atomic_test_bit(&manual_mask, i)) {
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
		TASK_LOG("Task 1 is running....\n");
		led_toggle(0);
		k_msleep(1000);
	}
}



/* ---------------------------------------------------------------- shell */

/* Parse "<n>" and check that LED n exists on this board */
static int led_arg(const struct shell *sh, const char *arg)
{
	char *end;
	long i = strtol(arg, &end, 10);

	if (*end != '\0' || i < 0 || i >= ARRAY_SIZE(leds)) {
		shell_error(sh, "bad LED number '%s' (0..%d)", arg, ARRAY_SIZE(leds) - 1);
		return -EINVAL;
	}
	if (leds[i].port == NULL) {
		shell_error(sh, "led%ld does not exist on this board", i);
		return -ENODEV;
	}
	return (int)i;
}

static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{
	int i = led_arg(sh, argv[1]);

	if (i < 0) {
		return i;
	}
	atomic_set_bit(&manual_mask, i);
	gpio_pin_set_dt(&leds[i], 1);
	shell_print(sh, "led%d ON (manual)", i);
	return 0;
}

static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
	int i = led_arg(sh, argv[1]);

	if (i < 0) {
		return i;
	}
	atomic_set_bit(&manual_mask, i);
	gpio_pin_set_dt(&leds[i], 0);
	shell_print(sh, "led%d OFF (manual)", i);
	return 0;
}

static int cmd_led_toggle(const struct shell *sh, size_t argc, char **argv)
{
	int i = led_arg(sh, argv[1]);

	if (i < 0) {
		return i;
	}
	atomic_set_bit(&manual_mask, i);
	gpio_pin_toggle_dt(&leds[i]);
	shell_print(sh, "led%d toggled (manual)", i);
	return 0;
}

static int cmd_led_blink(const struct shell *sh, size_t argc, char **argv)
{
	int i = led_arg(sh, argv[1]);

	if (i < 0) {
		return i;
	}
	atomic_clear_bit(&manual_mask, i);
	shell_print(sh, "led%d back to task T%d", i, i + 1);
	return 0;
}

static int cmd_led_status(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (leds[i].port == NULL) {
			shell_print(sh, "led%d: not on this board", i);
			continue;
		}
		shell_print(sh, "led%d: %s  %s", i,
			    gpio_pin_get_dt(&leds[i]) ? "ON " : "OFF",
			    atomic_test_bit(&manual_mask, i) ? "manual" : "blinking (task)");
	}
	return 0;
}

static int cmd_led_log(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "on") == 0) {
		atomic_set(&log_on, 1);
	} else if (strcmp(argv[1], "off") == 0) {
		atomic_set(&log_on, 0);
	} else {
		shell_error(sh, "usage: led log on|off");
		return -EINVAL;
	}
	return 0;
}

/* "led" command and its subcommands.
 * SHELL_CMD_ARG(name, subcmds, help, handler, mandatory args, optional args):
 * mandatory args count the command name itself, so 2 = "<cmd> <n>". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
	SHELL_CMD_ARG(on, NULL, "Turn LED on: led on <n>", cmd_led_on, 2, 0),
	SHELL_CMD_ARG(off, NULL, "Turn LED off: led off <n>", cmd_led_off, 2, 0),
	SHELL_CMD_ARG(toggle, NULL, "Toggle LED: led toggle <n>", cmd_led_toggle, 2, 0),
	SHELL_CMD_ARG(blink, NULL, "Give LED back to its task: led blink <n>",
		      cmd_led_blink, 2, 0),
	SHELL_CMD(status, NULL, "Show the 4 LEDs", cmd_led_status),
	SHELL_CMD_ARG(log, NULL, "Task messages: led log on|off", cmd_led_log, 2, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(led, &sub_led, "Control the board LEDs", NULL);

/* ----------------------------------------------------------------- main */

int main(void)
{
	printk("LAB RTOS (Zephyr) ------------------- \n");

	leds_init();

	/* K_NO_WAIT = start now. main() has priority 0 (highest), so the new
	 * threads only run once main() returns. */
	k_thread_create(&thread1, stack1, STACK_SIZE, task1, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread1, "T1");


	/* No vTaskStartScheduler(): returning from main() is allowed in Zephyr. */
	return 0;
}
