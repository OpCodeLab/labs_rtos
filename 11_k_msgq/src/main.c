/*
 * OpCode Labs - Zephyr training
 * Lab 11: k_msgq - send COPIES of small messages between threads.
 *
 *   producer --(k_msgq_put)--> msgq (5 messages) --(k_msgq_get)--> consumer
 *
 * The producer sends one message every 500 ms. The consumer prints it and
 * toggles LED0. The message is copied into the queue, so the producer can
 * change its local variable right after k_msgq_put().
 *
 *   FreeRTOS                               Zephyr
 *   -------------------------------------  ----------------------------------
 *   xQueueCreate(5, sizeof(msg))           K_MSGQ_DEFINE(msgq, sizeof(msg), 5, 4)
 *   xQueueSend(q, &msg, portMAX_DELAY)     k_msgq_put(&msgq, &msg, K_FOREVER)
 *   xQueueReceive(q, &msg, portMAX_DELAY)  k_msgq_get(&msgq, &msg, K_FOREVER)
 *   uxQueueMessagesWaiting(q)              k_msgq_num_used_get(&msgq)
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE  1024
#define PRIORITY    7

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

struct message {
	uint32_t count;
	int32_t temperature;
};

/* Queue of 5 messages of sizeof(struct message) bytes, aligned on 4 bytes */
K_MSGQ_DEFINE(msgq, sizeof(struct message), 5, 4);

static void producer(void *p1, void *p2, void *p3)
{
	struct message msg = { .count = 0, .temperature = 20 };

	while (1) {
		msg.count++;
		msg.temperature = 20 + (msg.count % 10);

		/* Copy msg into the queue. K_FOREVER: wait if the queue is full */
		k_msgq_put(&msgq, &msg, K_FOREVER);
		printk("producer: sent #%u (%u in queue)\n",
		       msg.count, k_msgq_num_used_get(&msgq));

		k_msleep(500);
	}
}

static void consumer(void *p1, void *p2, void *p3)
{
	struct message msg;

	while (1) {
		/* Sleeps here until a message arrives, then copies it into msg */
		k_msgq_get(&msgq, &msg, K_FOREVER);

		printk("consumer: got  #%u temperature %d C\n", msg.count, msg.temperature);
		if (led.port != NULL) {
			gpio_pin_toggle_dt(&led);
		}
	}
}

K_THREAD_DEFINE(producer_tid, STACK_SIZE, producer, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(consumer_tid, STACK_SIZE, consumer, NULL, NULL, NULL, PRIORITY, 0, 0);


/* Button ISR (FreeRTOS: HAL_GPIO_EXTI_Callback).
 * No portYIELD_FROM_ISR(): Zephyr reschedules by itself on ISR exit if a
 * higher priority thread became ready. */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{

}

int main(void)
{
	printk("LAB 11 k_msgq ------------------- \n");

	if (!gpio_is_ready_dt(&button)) 
	{
		printk("GPIO button not ready\n");
		return 0;
	}

	if (led.port != NULL && gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	return 0;
}
