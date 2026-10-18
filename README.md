# Zephyr labs – port of the FreeRTOS labs

Each folder is a Zephyr application that ports one lab from `../freertos_lab/`.
The application logic, task names, priorities and timings stay the same, so the
two versions can be compared side by side. Each `main.c` starts with a
FreeRTOS → Zephyr table for the APIs that the lab uses.

| Zephyr lab               | FreeRTOS source        | Topic                                                         |
|--------------------------|------------------------|---------------------------------------------------------------|
| `01-multitask`           | `main.c` (MAIN1)       | 4 periodic threads, one LED each                              |
| `02-button-sem-queue`    | `main6.c` (MAIN6)      | button ISR → semaphore → pulse width → message queue          |
| `03-software-timer`      | `main10.c` (MAIN10)    | debounce + auto-reload software timer (`k_timer`)             |
| `04-rms-scheduling`      | `main_rms.c` (RMS)     | Rate Monotonic: absolute-time periodic threads, CPU load      |
| `05-uart-protocol`       | `apk5.c` (APK5)        | UART RX IRQ state machine → msgq → link layer, shared UART    |
| `06-washing-machine`     | `apk6.c` (APK6)        | 3 threads, 2 state machines, `k_thread_suspend/resume`        |

### IPC labs (Zephyr only, beginner level)

Labs 10 to 14 are small labs: one producer thread, one consumer thread and one
IPC object each. LED0 toggles when the consumer gets data.
[IPC.md](IPC.md) explains all the IPC objects and how to choose one, and the diagram
[IPC_labs.excalidraw](IPC_labs.excalidraw) shows each lab step by step.

| Zephyr lab    | Object               | Topic                                               |
|---------------|----------------------|-----------------------------------------------------|
| `10_k_queue`  | `k_fifo` / `k_lifo`  | pass pointers; FIFO order vs. LIFO order            |
| `11_k_msgq`   | `k_msgq`             | pass copies of a small struct                       |
| `12_k_mbox`   | `k_mbox`             | synchronous: the sender waits for the receiver      |
| `13_zbus`     | zbus                 | publish/subscribe: one publisher, two observers     |
| `14_k_pipe`   | `k_pipe`             | stream of bytes, read one byte at a time            |

## Build / flash

From the repository root (same as `labs_rtos/01-blinky`):

```bash
west build -b $BOARD labs_rtos/02-button-sem-queue -d build/z02
west flash -d build/z02
```

The labs build for `stm32f4_disco`, `nucleo_f446re`, `nucleo_f429zi` and
`nucleo_g474re`. The FreeRTOS labs target the STM32F407 Discovery, which has
4 LEDs (aliases `led0..led3` = PD12..PD15). The Nucleo boards only have `led0`,
so the code skips the missing LEDs. The button is the alias `sw0`. Labs 05/06 use
the console UART (`zephyr,console`) for the protocol, the same way the FreeRTOS
labs share USART2 between `printf` and the frames.

## FreeRTOS → Zephyr cheat sheet

| Concept              | FreeRTOS                                     | Zephyr                                                         |
|----------------------|----------------------------------------------|----------------------------------------------------------------|
| Create a task        | `xTaskCreate(fn, "name", words, arg, prio, &h)` | `k_thread_create(&thr, stack, bytes, fn, p1, p2, p3, prio, 0, K_NO_WAIT)` or `K_THREAD_DEFINE` |
| Start the scheduler  | `vTaskStartScheduler()`                      | none: the kernel already runs `main()`, and `main()` may return |
| Priority             | bigger number = more urgent                  | **smaller** number = more urgent (these labs use `10 - prio`) |
| Delay                | `vTaskDelay(ticks)`                          | `k_msleep(ms)` / `k_sleep(K_MSEC(ms))`                         |
| Periodic delay       | `vTaskDelayUntil(&last, T)`                  | `next += T; k_sleep(K_TIMEOUT_ABS_MS(next))`                   |
| Tick count           | `xTaskGetTickCount()`                        | `k_uptime_get_32()` (ms) / `k_uptime_ticks()`                  |
| Busy wait            | `HAL_Delay(ms)`                              | `k_busy_wait(us)`                                              |
| Binary semaphore     | `xSemaphoreCreateBinary()`                   | `K_SEM_DEFINE(s, 0, 1)`                                        |
| Take / give          | `xSemaphoreTake(s, portMAX_DELAY)` / `Give`  | `k_sem_take(&s, K_FOREVER)` / `k_sem_give(&s)`                 |
| Mutex                | `xSemaphoreCreateMutex()`                    | `K_MUTEX_DEFINE(m)`, `k_mutex_lock/unlock`                     |
| Queue                | `xQueueCreate(n, size)`                      | `K_MSGQ_DEFINE(q, size, n, align)`                             |
| Send / receive       | `xQueueSend` / `xQueueReceive`               | `k_msgq_put` / `k_msgq_get`                                    |
| Software timer       | `xTimerCreate`, `xTimerStart/Stop`           | `K_TIMER_DEFINE`, `k_timer_start/stop`                         |
| Timer callback runs in | timer service task (thread)                | **ISR context**: use `k_work_submit()` to run the job in a thread |
| Suspend / resume     | `vTaskSuspend(h)` / `vTaskResume(h)`         | `k_thread_suspend(tid)` / `k_thread_resume(tid)`               |
| From an ISR          | `...FromISR()` + `portYIELD_FROM_ISR()`      | same API (with `K_NO_WAIT`); rescheduling on ISR exit is automatic |
| GPIO                 | `HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12)`     | `gpio_pin_toggle_dt(&led)`, pins from the devicetree           |
| EXTI callback        | `HAL_GPIO_EXTI_Callback()`                   | `gpio_pin_interrupt_configure_dt` + `gpio_add_callback`        |
| UART RX IRQ          | `HAL_UART_Receive_IT` + `RxCpltCallback`     | `uart_irq_callback_set` + `uart_irq_rx_enable` + `uart_fifo_read` |
| Configuration        | `FreeRTOSConfig.h`                           | `prj.conf` (Kconfig), e.g. `CONFIG_TIMESLICING`                |
| Stack size           | in **words** (128 = 512 bytes)               | in **bytes**                                                   |
