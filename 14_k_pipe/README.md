# Lab 14: `k_pipe`

```
writer --(k_pipe_write)--> pipe (16 bytes) --(k_pipe_read)--> reader --> LED0
```

A pipe carries a **stream of bytes**, like a UART. It has no message boundaries.
This is the FreeRTOS stream buffer.

- The writer sends `"line N from the writer\n"` (23 bytes) every second.
- The pipe only holds 16 bytes, so the writer waits until the reader has read some.
- The reader reads **one byte at a time** and prints the line when it sees `'\n'`.
- `k_pipe_write` blocks until **all** bytes are written. `k_pipe_read` waits for **all** the bytes you ask for.

## Run

```bash
west build -p always -b $BOARD labs_rtos/14_k_pipe -d build/rtos/14_k_pipe
west flash -d build/rtos/14_k_pipe
```

## Expected output

```
writer: wrote 23 bytes
reader: "line 0 from the writer"
```

## Exercises

1. Read 8 bytes at a time instead of 1. Why does the reader now wait for more lines? (It needs 8 bytes.)
2. Make the pipe 64 bytes and add `k_msleep(3000)` in the reader. How many lines fit before the writer blocks?
3. Add a second writer. Why can the lines get mixed when the pipe is full? (Hint: protect each line with a `k_mutex`.)
