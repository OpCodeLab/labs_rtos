# Lab 11: `k_msgq`

```
producer --(k_msgq_put)--> msgq (5 messages) --(k_msgq_get)--> consumer --> LED0
```

A message queue stores **copies** of fixed-size messages. This is the FreeRTOS queue.

- `K_MSGQ_DEFINE(msgq, sizeof(struct message), 5, 4)`: 5 messages, aligned on 4 bytes.
- The producer can change its local `msg` right after `k_msgq_put`, because the queue has a copy.
- When the queue is full, `k_msgq_put(.., K_FOREVER)` waits for space.

## Run

```bash
west build -p always -b $BOARD labs_rtos/11_k_msgq -d build/rtos/11_k_msgq
west flash -d build/rtos/11_k_msgq
```

## Expected output

```
producer: sent #1 (0 in queue)
consumer: got  #1 temperature 21 C
producer: sent #2 (0 in queue)
consumer: got  #2 temperature 22 C
```

"0 in queue": the consumer was already waiting, so the message went straight to it.

## Exercises

1. Add `k_msleep(2000)` in the consumer loop. Watch the queue fill up to 5, then the producer waits.
2. Use `K_NO_WAIT` in `k_msgq_put` and print the return value when the queue is full (`-ENOMSG`).
3. Add a second producer thread with another `count` range. Both share the same queue.
