# Lab 13: zbus (publish / subscribe)

```
                             +--> printer  (listener: callback)
publisher --> temp_chan -----+
                             +--> led_sub  (subscriber: thread) --> LED0
```

- A **channel** holds one message (the latest value) and a list of **observers**.
- The publisher calls `zbus_chan_pub()`; it does not know who receives the message.
- **Listener:** a callback, called inside `zbus_chan_pub()`. It must be short.
- **Subscriber:** a thread that sleeps in `zbus_sub_wait()`, then reads the channel with `zbus_chan_read()`.
- `prj.conf` needs `CONFIG_ZBUS=y`.

## Run

```bash
west build -p always -b $BOARD labs_rtos/13_zbus -d build/rtos/13_zbus
west flash -d build/rtos/13_zbus
```

## Expected output

```
publisher: publish #1
printer:   #1 temperature 21 C      <- listener, runs during the publish
led_sub:   #1 -> toggle LED         <- subscriber thread, runs after
```

## Exercises

1. Add a third observer (a listener that prints only when the temperature is above 25 C), without changing the publisher.
2. Remove `led_sub` from `ZBUS_OBSERVERS(...)`. What happens to the LED?
3. Publish 5 times quickly (no sleep). Does `led_sub` see all 5 values? (A subscriber reads the *latest* value.)
