/*
 * OpCode Labs - Zephyr training
 * Lab 14: k_pipe - send a stream of BYTES between threads.
 *
 *   writer --(k_pipe_write)--> pipe (16 bytes) --(k_pipe_read)--> reader
 *
 * The writer sends a text line every second. The reader reads the pipe
 * ONE BYTE at a time and rebuilds the line until it sees '\n'.
 * A pipe has no message boundaries: only bytes. The '\n' marks the end.
 *
 *   FreeRTOS                               Zephyr
 *   -------------------------------------  ----------------------------------
 *   xStreamBufferCreate(16, 1)             K_PIPE_DEFINE(pipe, 16, 4)
 *   xStreamBufferSend(sb, p, n, timeout)   k_pipe_write(&pipe, p, n, timeout)
 *   xStreamBufferReceive(sb, p, n, tmo)    k_pipe_read(&pipe, p, n, timeout)
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#define STACK_SIZE  1024
#define PRIORITY    7

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* 16 bytes ring buffer. A line is longer: the writer waits for the reader */
K_PIPE_DEFINE(pipe, 16, 4);

static void writer(void *p1, void *p2, void *p3)
{
	char line[40];
	int n = 0;

	while (1) {
		snprintk(line, sizeof(line), "line %d from the writer\n", n++);

		/* Blocks until ALL the bytes are in the pipe */
		int written = k_pipe_write(&pipe, (const uint8_t *)line, strlen(line), K_FOREVER);

		printk("writer: wrote %d bytes\n", written);
		k_msleep(1000);
	}
}

static void reader(void *p1, void *p2, void *p3)
{
	char line[40];
	size_t len = 0;
	uint8_t c;

	while (1) {
		/* Read 1 byte. Sleeps until a byte is available */
		k_pipe_read(&pipe, &c, 1, K_FOREVER);

		if (c == '\n' || len == sizeof(line) - 1) {
			line[len] = '\0';
			printk("reader: \"%s\"\n", line);
			len = 0;
			if (led.port != NULL) {
				gpio_pin_toggle_dt(&led);
			}
		} else {
			line[len++] = c;
		}
	}
}

K_THREAD_DEFINE(writer_tid, STACK_SIZE, writer, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(reader_tid, STACK_SIZE, reader, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
	printk("LAB 14 k_pipe ------------------- \n");

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
	return 0;
}
