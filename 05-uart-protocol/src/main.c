/*
 * OpCode Labs - Zephyr training
 * Lab 05: simple UART protocol between the board and a Python host.
 *
 * Port of freertos_lab/apk5.c (APK5).
 *
 *  Frame format (3 bytes) :   | SOF | CMD | DATA |
 *                              0xAA
 *
 *  Response (board -> PC) :   | SOF | CMD | STATUS/DATA |   (CMD with bit7 set)
 *
 *  Architecture :
 *
 *   UART RX IRQ (1 byte)            frame_msgq            link_layer thread
 *  +-----------------------+     +-----------+      +----------------------+
 *  | uart_isr()            | --> | frame_t   | ---> | decode CMD           |
 *  |  WAIT_SOF             |     | frame_t   |      | execute handler      |
 *  |  WAIT_CMD             |     | ...       |      | send response frame  |
 *  |  WAIT_DATA            |     +-----------+      +----------------------+
 *  +-----------------------+
 *
 *  The protocol uses the console UART (devicetree chosen zephyr,console):
 *  USART2 on stm32f4_disco / nucleo_f446re, USART3 on nucleo_f429zi,
 *  LPUART1 on nucleo_g474re. Logs (printk) share it, see app_log.h.
 *
 *   FreeRTOS / STM32 HAL                       Zephyr
 *   -----------------------------------------  -----------------------------------
 *   extern UART_HandleTypeDef huart2           DEVICE_DT_GET(DT_CHOSEN(zephyr_console))
 *   HAL_UART_Receive_IT(&huart2, &b, 1)        uart_irq_callback_set() +
 *                                              uart_irq_rx_enable()  (stays armed)
 *   HAL_UART_RxCpltCallback()                  uart_isr(): uart_fifo_read()
 *   HAL_UART_ErrorCallback()                   uart_err_check() inside uart_isr()
 *   HAL_UART_Transmit(&huart2, tx, 3, 100)     uart_poll_out() x 3
 *   xQueueCreate(8, sizeof(Frame_t))           K_MSGQ_DEFINE(frame_msgq, ...)
 *   xQueueSendFromISR + portYIELD_FROM_ISR     k_msgq_put(..., K_NO_WAIT)
 *   xTaskGetTickCountFromISR()                 k_uptime_get_32()  (ISR safe)
 *   xSemaphoreCreateMutex() (xUartMutex)       K_MUTEX_DEFINE(uart_mutex)
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include "app_log.h"

/* Protocol definitions ------------------------------------------------------*/
#define FRAME_SOF            0xAAu
#define FRAME_RSP_FLAG       0x80u   /* response CMD = request CMD | 0x80   */
#define FRAME_TIMEOUT_MS     50u     /* max gap between 2 bytes of a frame  */
#define QUEUE_FRAME_LEN      8u

/* Commands (PC -> board) */
#define CMD_PING             0x01u   /* DATA echoed back                    */
#define CMD_LED_SET          0x02u   /* DATA bit0..3 -> LED led0..led3      */
#define CMD_LED_TOGGLE       0x03u   /* DATA = LED index 0..3               */
#define CMD_GET_BUTTON       0x04u   /* response DATA = button state        */

/* Status codes */
#define STATUS_OK            0x00u
#define STATUS_BAD_CMD       0xE1u
#define STATUS_BAD_DATA      0xE2u

#define LINK_STACK_SIZE      1024
#define LINK_PRIO            7       /* FreeRTOS 3 -> 10 - 3 */

/* Types ---------------------------------------------------------------------*/
typedef enum {
	WAIT_SOF = 0,
	WAIT_CMD,
	WAIT_DATA
} rx_state_t;

typedef struct {
	uint8_t cmd;
	uint8_t data;
} frame_t;

/* Private variables ---------------------------------------------------------*/
#define LED_SPEC(alias) GPIO_DT_SPEC_GET_OR(DT_ALIAS(alias), gpios, {0})

static const struct gpio_dt_spec leds[4] = {
	LED_SPEC(led0), LED_SPEC(led1), LED_SPEC(led2), LED_SPEC(led3),
};
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

K_MUTEX_DEFINE(uart_mutex);
K_MSGQ_DEFINE(frame_msgq, sizeof(frame_t), QUEUE_FRAME_LEN, 1);

K_THREAD_STACK_DEFINE(link_stack, LINK_STACK_SIZE);
static struct k_thread link_thread;

static rx_state_t rx_state = WAIT_SOF;
static frame_t    rx_frame;
static uint32_t   last_byte_ms;

/* Private function prototypes -----------------------------------------------*/
static uint8_t decode_frame(const frame_t *frame);
static void    send_frame(uint8_t cmd, uint8_t data);


/* link_layer thread: blocked on frame_msgq, decodes and answers each frame */
static void link_layer(void *p1, void *p2, void *p3)
{
	frame_t frame;
	uint8_t rsp_data;

	while (1) {
		/* Blocked until the ISR posts a complete frame */
		k_msgq_get(&frame_msgq, &frame, K_FOREVER);

		LOG_INFO(COLOR_CYAN, "RX frame CMD=0x%02X DATA=0x%02X", frame.cmd, frame.data);

		rsp_data = decode_frame(&frame);
		send_frame(frame.cmd | FRAME_RSP_FLAG, rsp_data);
	}
}


/* Execute the command, return the DATA byte of the response */
static uint8_t decode_frame(const frame_t *frame)
{
	switch (frame->cmd) {
	case CMD_PING:
		return frame->data;

	case CMD_LED_SET:
		for (uint8_t i = 0; i < 4; i++) {
			if (leds[i].port != NULL) {
				gpio_pin_set_dt(&leds[i], (frame->data & (1u << i)) ? 1 : 0);
			}
		}
		return STATUS_OK;

	case CMD_LED_TOGGLE:
		if (frame->data > 3) {
			LOG_ERROR("LED index %u out of range", frame->data);
			return STATUS_BAD_DATA;
		}
		if (leds[frame->data].port != NULL) {
			gpio_pin_toggle_dt(&leds[frame->data]);
		}
		return STATUS_OK;

	case CMD_GET_BUTTON:
		return (gpio_pin_get_dt(&button) == 1) ? 1u : 0u;

	default:
		LOG_ERROR("Unknown CMD 0x%02X", frame->cmd);
		return STATUS_BAD_CMD;
	}
}


/* Send a 3-byte frame, protected by the same mutex as the logs */
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

	/* Inter-byte timeout : drop a half-received frame */
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
		/* Frame complete -> hand it to the link_layer thread.
		 * No portYIELD_FROM_ISR(): Zephyr reschedules on ISR exit. */
		k_msgq_put(&frame_msgq, &rx_frame, K_NO_WAIT);
		rx_state = WAIT_SOF;
		break;

	default:
		rx_state = WAIT_SOF;
		break;
	}
}


/* UART interrupt callback (FreeRTOS: HAL_UART_RxCpltCallback + ErrorCallback).
 * Unlike HAL_UART_Receive_IT(), the RX interrupt stays enabled: no re-arm. */
static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t byte;

	if (!uart_irq_update(dev)) {
		return;
	}

	/* Overrun, noise, framing: restart reception from scratch */
	if (uart_err_check(dev) != 0) {
		rx_state = WAIT_SOF;
	}

	while (uart_irq_rx_ready(dev) && (uart_fifo_read(dev, &byte, 1) == 1)) {
		rx_byte(byte);
	}
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

	k_thread_create(&link_thread, link_stack, LINK_STACK_SIZE, link_layer,
			NULL, NULL, NULL, LINK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&link_thread, "LinkLayer");

	LOG_INFO(COLOR_WHITE, " RTOS LAB APK5 (UART protocol, Zephyr) STARTING-----------------");

	/* Arm reception (FreeRTOS: HAL_UART_Receive_IT for the first byte) */
	uart_irq_callback_set(uart_dev, uart_isr);
	uart_irq_rx_enable(uart_dev);

	return 0;
}
