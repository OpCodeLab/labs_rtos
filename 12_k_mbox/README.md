# Lab 12: `k_mbox`

```
sender --(k_mbox_put)--> mbox --(k_mbox_get)--> receiver --> LED0
```

A mailbox has **no buffer**: `k_mbox_put` blocks until the receiver takes the message.
Then the data is copied straight from the sender's buffer to the receiver's buffer.

- The receiver sleeps 2 s before each `k_mbox_get`, so the sender waits about 2 s.
- The `info` field is **exchanged**: the receiver sees 100 (from the sender), the sender gets 200 back (from the receiver).
- `K_ANY` = any thread. You can also give a thread id to send to (or receive from) one thread only.

## Run

```bash
west build -p always -b $BOARD labs_rtos/12_k_mbox -d build/rtos/12_k_mbox
west flash -d build/rtos/12_k_mbox
```

## Expected output

```
sender:   put "hello 0"
receiver: got "hello 0" (32 bytes, info 100)
sender:   done, waited 2010 ms, reply info = 200
```

## Exercises

1. Remove the `k_msleep(2000)` in the receiver. How long does the sender wait now?
2. Use `K_MSEC(500)` instead of `K_FOREVER` in `k_mbox_put`. What does it return?
3. Compare with lab 11: replace the mailbox by a `k_msgq`. Does the sender still wait?
