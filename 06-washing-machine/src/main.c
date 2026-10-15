/*
 * OpCode Labs - Zephyr training
 * Lab 06: washing machine application on top of the Lab 05 UART link.
 *
 * Port of freertos_lab/apk6.c (APK6).
 *
 *  Frame format (3 bytes) :   | SOF 0xAA | CMD | DATA |
 *
 *  Host -> board (requests, answered with CMD|0x80 + status)
 *     0x01 PING          data echoed
 *     0x10 CFG_PROGRAM   0 Cotton, 1 Synthetic, 2 Wool, 3 Quick
 *     0x11 CFG_TEMP      20, 30, 40, 60 or 90 (deg C)
 *     0x12 CFG_SPIN      rpm / 100, 4..14
 *     0x20 START_CYCLE   data ignored
 *     0x21 GET_STATUS    answer data = system state (0 IDLE, 1 READY, 2 RUN)
 *                        + bit7 set when the sensor is in ERROR_STATE (door open)
 *
 *  Board -> Host (events, not answers)
 *     0x30 EVT_STEP      data = step 1..6 entered
 *     0x31 EVT_PROGRESS  data = progress of the current step 0..100 %
 *     0x32 EVT_DONE      cycle finished
 *     0x33 EVT_DOOR      data = 1 open, 0 closed
 *     0x34 EVT_ERROR     data = error code (0x01 door open during cycle)
 *
 *  Threads (FreeRTOS prio -> Zephyr prio = 10 - FreeRTOS prio) :
 *     link_layer  (3 -> 7) : decodes frames, answers PING / GET_STATUS,
 *                            rejects application commands while a cycle runs,
 *                            forwards the others to system_task through cmd_msgq
 *     system_task (2 -> 8) : state machine IDLE / READY / RUN
 *     sensor_task (4 -> 6) : state machine on the user button (door)
 *                              NO_ERROR_STATE : press -> door open,
 *                                               suspend system_task if running
 *                              ERROR_STATE    : press -> door closed,
 *                                               resume system_task if suspended
 *
 *   FreeRTOS                         Zephyr
 *   -------------------------------  -------------------------------------------
 *   TaskHandle_t hSystemTask         struct k_thread system_thread (k_tid_t = &...)
 *   vTaskSuspend(hSystemTask)        k_thread_suspend(&system_thread)
 *   vTaskResume(hSystemTask)         k_thread_resume(&system_thread)
 *   xQueueSend(QueueCmd, &f, 0)      k_msgq_put(&cmd_msgq, &f, K_NO_WAIT)
 *   pdPASS                           0 (Zephyr calls return 0 or -errno)
 *   pdMS_TO_TICKS(ms) + vTaskDelay   k_msleep(ms)
 *
 *  Both kernels behave the same when a sleeping thread is suspended: the
 *  sleep is cancelled, and after resume the delay returns at once.
 *
 *  LEDs (stm32f4_disco): led0 green RUN, led1 orange CONFIG, led2 red DOOR,
 *  led3 blue DONE. Nucleo boards only have led0 (RUN).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include "app_log.h"

/* Protocol ------------------------------------------------------------------*/
#define FRAME_SOF            0xAAu
#define FRAME_RSP_FLAG       0x80u
#define FRAME_TIMEOUT_MS     50u

#define CMD_PING             0x01u
#define CMD_CFG_PROGRAM      0x10u
#define CMD_CFG_TEMP         0x11u
#define CMD_CFG_SPIN         0x12u
#define CMD_START_CYCLE      0x20u
#define CMD_GET_STATUS       0x21u

#define EVT_STEP             0x30u
#define EVT_PROGRESS         0x31u
#define EVT_DONE             0x32u
#define EVT_DOOR             0x33u
#define EVT_ERROR            0x34u

#define STATUS_OK            0x00u
#define STATUS_BAD_CMD       0xE1u
#define STATUS_BAD_DATA      0xE2u
#define STATUS_BUSY          0xE3u   /* cycle running: no config, no start */
#define STATUS_NOT_CONFIG    0xE4u   /* start without a complete config    */
#define STATUS_DOOR_OPEN     0xE5u   /* start with the door open           */
#define STATUS_ERROR_FLAG    0x80u   /* GET_STATUS: sensor in ERROR_STATE   */

#define ERR_DOOR_OPEN        0x01u

/* Application ---------------------------------------------------------------*/
#define NB_STEPS             6u
#define STEP_TIME_MS         10000u  /* duration of one step               */
#define TICK_MS              100u    /* system_task period in RUN          */
#define PROGRESS_EVERY       2u      /* EVT_PROGRESS every 2 ticks = 200 ms */
#define DEBOUNCE_MS          30u

#define CFG_PROGRAM_BIT      (1u << 0)
#define CFG_TEMP_BIT         (1u << 1)
#define CFG_SPIN_BIT         (1u << 2)
#define CFG_COMPLETE         (CFG_PROGRAM_BIT | CFG_TEMP_BIT | CFG_SPIN_BIT)

#define LED_RUN              0   /* green : blinks while running */
#define LED_CONFIG           1   /* orange: configuration valid  */
#define LED_DOOR             2   /* red   : door open            */
#define LED_DONE             3   /* blue  : cycle finished       */

#define STACK_SIZE           1024
#define LINK_PRIO            7   /* FreeRTOS 3 */
#define SYSTEM_PRIO          8   /* FreeRTOS 2 */
#define SENSOR_PRIO          6   /* FreeRTOS 4 */

/* Types ---------------------------------------------------------------------*/
typedef enum { WAIT_SOF = 0, WAIT_CMD, WAIT_DATA } rx_state_t;
typedef enum { IDLE_STATE = 0, READY_STATE, RUN_STATE } sys_state_t;
typedef enum { NO_ERROR_STATE = 0, ERROR_STATE } sensor_state_t;

typedef struct {
	uint8_t cmd;
	uint8_t data;
} frame_t;

typedef struct {
	uint8_t program;
	uint8_t temp;
	uint8_t spin;
	uint8_t mask;          /* CFG_x_BIT received */
} config_t;

/* Private variables ---------------------------------------------------------*/
#define LED_SPEC(alias) GPIO_DT_SPEC_GET_OR(DT_ALIAS(alias), gpios, {0})

static const struct gpio_dt_spec leds[4] = {
	LED_SPEC(led0), LED_SPEC(led1), LED_SPEC(led2), LED_SPEC(led3),
};
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;
static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

K_MUTEX_DEFINE(uart_mutex);
K_MSGQ_DEFINE(frame_msgq, sizeof(frame_t), 8, 1);   /* ISR       -> link_layer  */
K_MSGQ_DEFINE(cmd_msgq, sizeof(frame_t), 4, 1);     /* link_layer -> system_task */
K_SEM_DEFINE(sem_door, 0, 1);                       /* button ISR -> sensor_task */

K_THREAD_STACK_DEFINE(link_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(system_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(sensor_stack, STACK_SIZE);
static struct k_thread link_thread, system_thread, sensor_thread;

/* Each state has a single writer: sys_state = system_task, sensor_state = sensor_task */
static volatile sys_state_t    sys_state    = IDLE_STATE;
static volatile sensor_state_t sensor_state = NO_ERROR_STATE;

static rx_state_t rx_state = WAIT_SOF;
static frame_t    rx_frame;
static uint32_t   last_byte_ms;

static const char *program_name[] = { "Cotton", "Synthetic", "Wool", "Quick" };
static const char *step_name[NB_STEPS] = { "Fill", "Heat", "Wash", "Rinse", "Drain", "Spin" };
static const uint8_t valid_temp[] = { 20, 30, 40, 60, 90 };

/* Private function prototypes -----------------------------------------------*/
static void    send_frame(uint8_t cmd, uint8_t data);
static uint8_t apply_config(config_t *cfg, const frame_t *frame);
static void    run_cycle(void);

static void led_set(int i, int value)
{
	if (leds[i].port != NULL) {
		gpio_pin_set_dt(&leds[i], value);
	}
}

static void led_toggle(int i)
{
	if (leds[i].port != NULL) {
		gpio_pin_toggle_dt(&leds[i]);
	}
}


/* ======================================================= link_layer =======*/
static void link_layer(void *p1, void *p2, void *p3)
{
	frame_t frame;

	while (1) {
		k_msgq_get(&frame_msgq, &frame, K_FOREVER);
		LOG_INFO(COLOR_CYAN, "RX frame CMD=0x%02X DATA=0x%02X", frame.cmd, frame.data);

		switch (frame.cmd) {
		/* Link level : answered here */
		case CMD_PING:
			send_frame(frame.cmd | FRAME_RSP_FLAG, frame.data);
			break;

		case CMD_GET_STATUS:
			send_frame(frame.cmd | FRAME_RSP_FLAG,
				   (uint8_t)sys_state |
				   ((sensor_state == ERROR_STATE) ? STATUS_ERROR_FLAG : 0u));
			break;

		/* Application level : only the system_task in IDLE/READY may handle them */
		case CMD_CFG_PROGRAM:
		case CMD_CFG_TEMP:
		case CMD_CFG_SPIN:
		case CMD_START_CYCLE:
			if (sys_state == RUN_STATE) {   /* also while suspended by the door */
				LOG_ERROR("Cycle running: CMD 0x%02X refused", frame.cmd);
				send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_BUSY);
			} else if (k_msgq_put(&cmd_msgq, &frame, K_NO_WAIT) != 0) {
				send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_BUSY);
			}
			break;

		default:
			LOG_ERROR("Unknown CMD 0x%02X", frame.cmd);
			send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_BAD_CMD);
			break;
		}
	}
}


/* ====================================================== system_task =======*/
static void system_task(void *p1, void *p2, void *p3)
{
	config_t cfg = { 0 };
	frame_t  frame;
	uint8_t  status;

	while (1) {
		switch (sys_state) {
		/* ---- IDLE_STATE : wait for a complete configuration ---- */
		case IDLE_STATE:
			k_msgq_get(&cmd_msgq, &frame, K_FOREVER);
			if (frame.cmd == CMD_START_CYCLE) {
				LOG_ERROR("Start refused: no configuration");
				send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_NOT_CONFIG);
				break;
			}
			status = apply_config(&cfg, &frame);
			send_frame(frame.cmd | FRAME_RSP_FLAG, status);
			if (cfg.mask == CFG_COMPLETE) {
				led_set(LED_CONFIG, 1);
				LOG_INFO(COLOR_GREEN, "Configured: %s, %uC, %u rpm -> READY_STATE",
					 program_name[cfg.program], cfg.temp, cfg.spin * 100u);
				sys_state = READY_STATE;
			}
			break;

		/* ---- READY_STATE : configuration valid, wait for START ---- */
		case READY_STATE:
			k_msgq_get(&cmd_msgq, &frame, K_FOREVER);
			if (frame.cmd != CMD_START_CYCLE) {
				/* a new parameter replaces the old one, the config stays complete */
				status = apply_config(&cfg, &frame);
				send_frame(frame.cmd | FRAME_RSP_FLAG, status);
				break;
			}
			if (sensor_state == ERROR_STATE) {
				LOG_ERROR("Start refused: door open");
				send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_DOOR_OPEN);
				break;
			}

			send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_OK);

			LOG_INFO(COLOR_GREEN, "Cycle start: %s, %uC, %u rpm -> RUN_STATE",
				 program_name[cfg.program], cfg.temp, cfg.spin * 100u);

			led_set(LED_DONE, 0);

			sys_state = RUN_STATE;
			break;

		/* ---- RUN_STATE : execute the cycle (may be suspended by sensor_task) ---- */
		case RUN_STATE:
			run_cycle();
			led_set(LED_RUN, 0);
			led_set(LED_DONE, 1);
			LOG_INFO(COLOR_GREEN, "Cycle done -> READY_STATE");
			sys_state = READY_STATE;           /* configuration is kept */
			send_frame(EVT_DONE, 0);
			break;

		default:
			sys_state = IDLE_STATE;
			break;
		}
	}
}


/* Validate and store one configuration parameter */
static uint8_t apply_config(config_t *cfg, const frame_t *frame)
{
	switch (frame->cmd) {
	case CMD_CFG_PROGRAM:
		if (frame->data > 3) {
			return STATUS_BAD_DATA;
		}
		cfg->program = frame->data;
		cfg->mask |= CFG_PROGRAM_BIT;
		return STATUS_OK;

	case CMD_CFG_TEMP:
		for (uint8_t i = 0; i < sizeof(valid_temp); i++) {
			if (valid_temp[i] == frame->data) {
				cfg->temp = frame->data;
				cfg->mask |= CFG_TEMP_BIT;
				return STATUS_OK;
			}
		}
		return STATUS_BAD_DATA;

	case CMD_CFG_SPIN:
		if ((frame->data < 4) || (frame->data > 14)) {
			return STATUS_BAD_DATA;
		}
		cfg->spin = frame->data;
		cfg->mask |= CFG_SPIN_BIT;
		return STATUS_OK;

	default:
		return STATUS_BAD_CMD;
	}
}


/* Run the 6 steps. Time only advances while the thread runs, so a
 * suspension (door open) freezes the cycle where it is. */
static void run_cycle(void)
{
	const uint32_t ticks_per_step = STEP_TIME_MS / TICK_MS;
	frame_t frame;

	for (uint8_t step = 1; step <= NB_STEPS; step++) {
		send_frame(EVT_STEP, step);
		LOG_INFO(COLOR_YELLOW, "Step %u: %s", step, step_name[step - 1]);

		for (uint32_t tick = 1; tick <= ticks_per_step; tick++) {
			k_msleep(TICK_MS);

			/* A command queued just before RUN started: refuse it */
			while (k_msgq_get(&cmd_msgq, &frame, K_NO_WAIT) == 0) {
				send_frame(frame.cmd | FRAME_RSP_FLAG, STATUS_BUSY);
			}

			if ((tick % PROGRESS_EVERY) == 0) {
				send_frame(EVT_PROGRESS, (uint8_t)(tick * 100u / ticks_per_step));
				led_toggle(LED_RUN);
			}
		}
	}
}


/* ====================================================== sensor_task =======*/
/* Wait for one debounced press of the button (edge confirmed after 30 ms) */
static void wait_button_press(void)
{
	while (1) {
		k_sem_take(&sem_door, K_FOREVER);
		k_msleep(DEBOUNCE_MS);
		if (gpio_pin_get_dt(&button) == 1) {
			break;
		}
		(void)k_sem_take(&sem_door, K_NO_WAIT);
	}

	/* wait for release, then drop the edges caused by the bounce */
	while (gpio_pin_get_dt(&button) == 1) {
		k_msleep(10);
	}
	k_msleep(DEBOUNCE_MS);
	(void)k_sem_take(&sem_door, K_NO_WAIT);
}

static void sensor_task(void *p1, void *p2, void *p3)
{
	uint8_t suspended = 0;     /* 1 = this thread suspended the system_task */

	while (1) {
		wait_button_press();
		switch (sensor_state) {
		/* ---- NO_ERROR_STATE : door closed, a press opens it ---- */
		case NO_ERROR_STATE:
			led_set(LED_DOOR, 1);

			/* Take the UART mutex first: system_task is then sure not to own it,
			 * so suspending it cannot block the UART for everybody else. */
			k_mutex_lock(&uart_mutex, K_FOREVER);
			if (sys_state == RUN_STATE) {
				k_thread_suspend(&system_thread);
				suspended = 1;
			}
			k_mutex_unlock(&uart_mutex);

			send_frame(EVT_DOOR, 1);
			if (suspended) {
				send_frame(EVT_ERROR, ERR_DOOR_OPEN);
				LOG_ERROR("Door open: cycle suspended -> ERROR_STATE");
			}
			sensor_state = ERROR_STATE;
			break;

		/* ---- ERROR_STATE : door open, a press closes it ---- */
		case ERROR_STATE:
			led_set(LED_DOOR, 0);
			sensor_state = NO_ERROR_STATE;
			send_frame(EVT_DOOR, 0);
			if (suspended) {
				suspended = 0;
				LOG_INFO(COLOR_GREEN, "Door closed: cycle resumed -> NO_ERROR_STATE");
				k_thread_resume(&system_thread);
			}
			break;

		default:
			sensor_state = NO_ERROR_STATE;
			break;
		}
	}
}


/* ====================================================== Helpers / ISR =====*/
static void send_frame(uint8_t cmd, uint8_t data)
{
	const uint8_t tx[3] = { FRAME_SOF, cmd, data };

	k_mutex_lock(&uart_mutex, K_FOREVER);
	for (int i = 0; i < sizeof(tx); i++) {
		uart_poll_out(uart_dev, tx[i]);
	}
	k_mutex_unlock(&uart_mutex);
}


/* Byte-level state machine (ISR context) */
static void rx_byte(uint8_t byte)
{
	uint32_t now = k_uptime_get_32();

	if ((rx_state != WAIT_SOF) && ((now - last_byte_ms) > FRAME_TIMEOUT_MS)) {
		rx_state = WAIT_SOF;
	}
	last_byte_ms = now;

	switch (rx_state) {
	case WAIT_SOF:
		if (byte == FRAME_SOF) {
			rx_state = WAIT_CMD;
		}
		break;

	case WAIT_CMD:
		rx_frame.cmd = byte;
		rx_state     = WAIT_DATA;
		break;

	case WAIT_DATA:
		rx_frame.data = byte;
		k_msgq_put(&frame_msgq, &rx_frame, K_NO_WAIT);
		rx_state = WAIT_SOF;
		break;

	default:
		rx_state = WAIT_SOF;
		break;
	}
}


/* UART interrupt callback (FreeRTOS: HAL_UART_RxCpltCallback + ErrorCallback) */
static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t byte;

	if (!uart_irq_update(dev)) {
		return;
	}

	if (uart_err_check(dev) != 0) {
		rx_state = WAIT_SOF;
	}

	while (uart_irq_rx_ready(dev) && (uart_fifo_read(dev, &byte, 1) == 1)) {
		rx_byte(byte);
	}
}


/* User button = door sensor (FreeRTOS: HAL_GPIO_EXTI_Callback) */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	k_sem_give(&sem_door);
}


int main(void)
{
	if (!device_is_ready(uart_dev) || !gpio_is_ready_dt(&button)) {
		printk("UART or button not ready\n");
		return 0;
	}

	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (leds[i].port != NULL && gpio_is_ready_dt(&leds[i])) {
			gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		}
	}

	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	k_thread_create(&link_thread, link_stack, STACK_SIZE, link_layer,
			NULL, NULL, NULL, LINK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&link_thread, "LinkLayer");
	k_thread_create(&system_thread, system_stack, STACK_SIZE, system_task,
			NULL, NULL, NULL, SYSTEM_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&system_thread, "System");
	k_thread_create(&sensor_thread, sensor_stack, STACK_SIZE, sensor_task,
			NULL, NULL, NULL, SENSOR_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&sensor_thread, "Sensor");

	LOG_INFO(COLOR_WHITE, " RTOS LAB APK6 (washing machine, Zephyr) STARTING-----------------");

	uart_irq_callback_set(uart_dev, uart_isr);
	uart_irq_rx_enable(uart_dev);

	return 0;
}
