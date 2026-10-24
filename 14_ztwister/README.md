# Lab 14: unit tests with ztest + Twister

```
src/temp.c  (code under test)
     ^
     |  linked into
tests/  --(west twister)-->  build + run on native_sim (your PC)  -->  PASS / FAIL report
```

- **ztest** is the Zephyr unit test framework: `ZTEST(suite, test)` + `zassert_*()`.
- **Twister** is the Zephyr test runner: it finds `testcase.yaml`, builds the test, runs it, and writes a report.
- **native_sim** builds Zephyr as a Linux program: tests run on the PC, no board needed.
- Test plain C logic (no GPIO, no kernel calls) like this: it is fast and easy to test.

## Files

| File                     | Role                                                                 |
|--------------------------|----------------------------------------------------------------------|
| `src/temp.c`, `temp.h`   | the module under test (clamp, average, alarm)                        |
| `tests/src/test_temp.c`  | the test cases (`ZTEST`) and the suite (`ZTEST_SUITE`)               |
| `tests/prj.conf`         | `CONFIG_ZTEST=y`                                                      |
| `tests/CMakeLists.txt`   | builds the test file **and** `../src/temp.c`                         |
| `tests/testcase.yaml`    | tells Twister the test name (`training.temp`) and the platform       |
| `tests/test_config.yaml` | needed because the light Zephyr checkout has no `zephyr/tests/` folder |

## Run

```bash
west twister -c --test-config labs_rtos/14_ztwister/tests/test_config.yaml \
    -T labs_rtos/14_ztwister/tests -p native_sim -O build/rtos/14_ztwister -v
```

- `-T` where to look for `testcase.yaml`, `-p` the platform, `-O` the output folder.
- `-c` overwrite the old output folder (otherwise Twister renames it `.1`, `.2`, ...).
- Add `--inline-logs` to print the test output in the terminal when a test fails.

## Expected output

```
INFO    - 1/1 native_sim/native   training.temp   PASSED (native 0.015s <host>)
INFO    - 5 of 5 executed test cases passed (100.00%) on 1 out of total 145 platforms
```

Test console: `find build/rtos/14_ztwister -name handler.log`

```
START - test_alarm
 PASS - test_alarm in 0.000 seconds
...
SUITE PASS - 100.00% [temp]: pass = 5, fail = 0, skip = 0, total = 5
```

Reports: `build/rtos/14_ztwister/twister.json` and `twister.xml` (JUnit, for CI).

## Code coverage (HTML)

Add `--coverage` to see which lines of `temp.c` the tests run:

```bash
west twister -c --test-config labs_rtos/14_ztwister/tests/test_config.yaml \
    -T labs_rtos/14_ztwister/tests -p native_sim -O build/rtos/14_ztwister \
    --coverage --coverage-tool gcovr --coverage-formats html \
    --coverage-basedir labs_rtos/14_ztwister
```

- Open `build/rtos/14_ztwister/coverage/index.html` in a browser (green = run, red = never run).
- `--coverage-basedir` keeps only the lab files. Without it, the report also lists the Zephyr kernel files.
- Expected: `src/temp.c` lines 15/15 and branches 8/8 (100 %).

## Exercises

1. Add a bug in `temp_is_alarm()`: use `>=` instead of `>`. Run Twister again. Which test fails, and on which line?
2. Add a test for `temp_clamp()` when `t` is exactly `min` or `max`.
3. Add a new function `temp_max(const int *samples, size_t n)` to `temp.c`. Write its tests **first**, then the code.
4. Remove `test_average_empty` and run with `--coverage`. Which line turns red in the HTML report?
5. Add a `before` function to the suite (the 4th argument of `ZTEST_SUITE`) that prints `"start test"`. When does it run?
