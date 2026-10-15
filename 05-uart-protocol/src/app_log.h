/*
 * OpCode Labs - Zephyr training
 * Colored log macros shared with the binary protocol on the same UART.
 *
 * Zephyr equivalent of the FreeRTOS lab log.h: every print takes uart_mutex
 * (FreeRTOS: xUartMutex) so a log line never cuts a 3-byte frame in two.
 * The log text is pure ASCII (< 0x80): SOF = 0xAA never appears inside it.
 *
 * Note: Zephyr also has a logging subsystem (LOG_MODULE_REGISTER, LOG_INF,
 * LOG_ERR). It is not used here because its output thread would write to the
 * UART without our mutex and could interleave with the protocol frames.
 */
#ifndef APP_LOG_H
#define APP_LOG_H

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

extern struct k_mutex uart_mutex;

#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_WHITE   "\x1b[37m"

#define LOG_INFO(color, fmt, ...)                                         \
	do {                                                              \
		k_mutex_lock(&uart_mutex, K_FOREVER);                     \
		printk(color fmt COLOR_RESET "\r\n", ##__VA_ARGS__);      \
		k_mutex_unlock(&uart_mutex);                              \
	} while (0)

#define LOG_ERROR(fmt, ...) LOG_INFO(COLOR_RED, "ERROR: " fmt, ##__VA_ARGS__)

#endif /* APP_LOG_H */
