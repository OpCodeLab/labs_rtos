/*
 * OpCode Labs - Zephyr training
 * Lab 12: k_mbox - synchronous, ADDRESSED message.
 *
 *   sender --(k_mbox_put, target = receiver_tid)--> mbox --> receiver (source = sender_tid)
 *                                                       \-x-> spy      (source = K_ANY)
 *
 * A mailbox has NO buffer. k_mbox_put() blocks until the receiver has taken
 * the message. The receiver is slow (it needs 2 s per message), so the sender
 * waits about 2 s on each k_mbox_put(): look at the "waited" time.
 *
 * Addressing: a message is delivered only if BOTH sides agree:
 *   - the sender's tx_target_thread is K_ANY or the receiving thread, AND
 *   - the receiver's rx_source_thread is K_ANY or the sending thread.
 * The spy accepts messages from anyone (K_ANY), but the sender targets
 * receiver_tid only, so the spy never gets anything: its get times out.
 *
 * The 32-bit "info" field is exchanged: the sender gets the receiver's info
 * back (here a reply code).
 *
 *   FreeRTOS                          Zephyr
 *   --------------------------------  ----------------------------------------
 *   (no mailbox object)               K_MBOX_DEFINE(mbox)
 *                                     k_mbox_put(&mbox, &tx_msg, K_FOREVER)
 *                                     k_mbox_get(&mbox, &rx_msg, buffer, K_FOREVER)
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE  1024
#define PRIORITY    7

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

K_MBOX_DEFINE(mbox);

/* Thread IDs are defined at the bottom by K_THREAD_DEFINE(): declare them here */
extern const k_tid_t sender_tid;
extern const k_tid_t receiver_tid;
extern const k_tid_t spy_tid;

static void sender(void *p1, void *p2, void *p3)
{
	char text[32];
	int n = 0;

	while (1) {
		snprintk(text, sizeof(text), "hello %d", n++);

		struct k_mbox_msg tx = {
			.info = 100,			/* any value for the receiver */
			.size = sizeof(text),		/* bytes to send */
			.tx_data = text,		/* where the data is */
			.tx_target_thread = receiver_tid, /* ONLY the receiver */
            //.tx_target_thread = K_ANY, /* ANY thread (receiver or spy) */
		};
		uint32_t start = k_uptime_get_32();

		printk("sender:   put \"%s\" -> receiver only\n", text);
		k_mbox_put(&mbox, &tx, K_FOREVER);	/* blocks until taken */

		printk("sender:   done, waited %u ms, reply info = %u\n",
		       k_uptime_get_32() - start, tx.info);
	}
}

static void receiver(void *p1, void *p2, void *p3)
{
	char buffer[32];

	while (1) {
		k_msleep(2000);		/* slow receiver: the sender must wait */

		struct k_mbox_msg rx = {
			.info = 200,			/* reply sent back to the sender */
			.size = sizeof(buffer),		/* max bytes we accept */
			.rx_source_thread = sender_tid,	/* ONLY from the sender */
		};

		k_mbox_get(&mbox, &rx, buffer, K_FOREVER);	/* copies the data */

		printk("receiver: got \"%s\" (%u bytes, info %u, from %s)\n",
		       buffer, rx.size, rx.info,
		       rx.rx_source_thread == sender_tid ? "sender" : "unknown");
		if (led.port != NULL) {
			gpio_pin_toggle_dt(&led);
		}
	}
}

static void spy(void *p1, void *p2, void *p3)
{
	char buffer[32];

	while (1) {
		struct k_mbox_msg rx = {
			.info = 0,
			.size = sizeof(buffer),
			.rx_source_thread = K_ANY,	/* accepts anyone... */
		};

		/* ...but the sender targets receiver_tid, so nothing matches */
		int ret = k_mbox_get(&mbox, &rx, buffer, K_MSEC(3000));

		if (ret == 0) {
			printk("spy:      got \"%s\" (should never happen!)\n", buffer);
		} else {
			printk("spy:      nothing for me (ret = %d)\n", ret);
		}
	}
}

K_THREAD_DEFINE(sender_tid, STACK_SIZE, sender, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(receiver_tid, STACK_SIZE, receiver, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(spy_tid, STACK_SIZE, spy, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
	printk("LAB 12 k_mbox (addressed) ------- \n");

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
	return 0;
}