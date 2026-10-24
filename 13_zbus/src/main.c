/*
 * OpCode Labs - Zephyr training
 * Lab 13: zbus - publish / subscribe.
 *
 *                                  +--> printer  (listener: a callback)
 *   publisher --> temp_chan -------+
 *                                  +--> led_sub  (subscriber: a thread)
 *
 * The publisher does not know who receives the message. Every observer of
 * the channel is told when a new value is published.
 *   - listener   : callback, runs inside zbus_chan_pub(), must be short
 *   - subscriber : own thread, wakes up, then reads the channel
 *
 *   FreeRTOS                          Zephyr zbus
 *   --------------------------------  ----------------------------------------
 *   (no publish/subscribe object)     ZBUS_CHAN_DEFINE(chan, type, ..., observers, init)
 *                                     zbus_chan_pub(&chan, &msg, timeout)
 *                                     zbus_chan_read(&chan, &msg, timeout)
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>

#define STACK_SIZE  1024
#define PRIORITY    7

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* The message type of the channel */
struct temp_msg {
	uint32_t count;
	int32_t temperature;
};

/* The channel: message type + list of observers + initial value */
ZBUS_CHAN_DEFINE(temp_chan,				/* name */
		 struct temp_msg,			/* message type */
		 NULL,					/* validator (none) */
		 NULL,					/* user data (none) */
		 ZBUS_OBSERVERS(printer, led_sub),	/* who is notified */
		 ZBUS_MSG_INIT(.count = 0, .temperature = 0));

/* Observer 1: listener = a callback, called by zbus_chan_pub() */
static void printer_cb(const struct zbus_channel *chan)
{
	const struct temp_msg *msg = zbus_chan_const_msg(chan);

	printk("printer:   #%u temperature %d C\n", msg->count, msg->temperature);
}
ZBUS_LISTENER_DEFINE(printer, printer_cb);

/* Observer 2: subscriber = a thread with a small queue of notifications */
ZBUS_SUBSCRIBER_DEFINE(led_sub, 4);

static void led_thread(void *p1, void *p2, void *p3)
{
	const struct zbus_channel *chan;
	struct temp_msg msg;

	while (1) {
		/* Sleeps until a channel it observes is published */
		zbus_sub_wait(&led_sub, &chan, K_FOREVER);

		/* Read (copy) the current value of the channel */
		zbus_chan_read(chan, &msg, K_FOREVER);

		printk("led_sub:   #%u -> toggle LED\n", msg.count);
		if (led.port != NULL) {
			gpio_pin_toggle_dt(&led);
		}
	}
}

static void publisher(void *p1, void *p2, void *p3)
{
	struct temp_msg msg = { 0 };

	while (1) {
		msg.count++;
		msg.temperature = 20 + (msg.count % 10);

		printk("publisher: publish #%u\n", msg.count);
		zbus_chan_pub(&temp_chan, &msg, K_FOREVER);

		k_msleep(1000);
	}
}

K_THREAD_DEFINE(led_tid, STACK_SIZE, led_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(publisher_tid, STACK_SIZE, publisher, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
	printk("LAB 13 zbus ------------------- \n");

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
	return 0;
}
