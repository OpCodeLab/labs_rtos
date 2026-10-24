/*
 * OpCode Labs - Zephyr training
 * Lab 01: four periodic threads, each one toggles its own LED.
 *
 * Port of freertos_lab/main.c (MAIN1).
 *
 *   FreeRTOS                          Zephyr
 *   --------------------------------  -----------------------------------------
 *   HAL_Init / MX_GPIO_Init           devicetree + gpio_pin_configure_dt()
 *   xTaskCreate(Task1,"T1",128,..,2)  k_thread_create(&t1, stack1, ..., prio)
 *   vTaskStartScheduler()             nothing: the kernel already runs main()
 *   vTaskDelay(1000)                  k_msleep(1000)
 *   HAL_GPIO_TogglePin(GPIOD, PD12)   gpio_pin_toggle_dt(&leds[0])
 *   printf                            printk
 *
 * LEDs come from the devicetree aliases led0..led3. On stm32f4_disco they are
 * PD12 green, PD13 orange, PD14 red, PD15 blue (same pins as the FreeRTOS lab).
 * Nucleo boards only have led0: the missing LEDs are skipped.
 *
 * littlefs (CONFIG_FILE_SYSTEM_LITTLEFS=y), mounted on /lfs:
 *   the file system lives in the flash partition "storage_partition"
 *   (boards/stm32f4_disco.overlay: last 512 KB of the internal flash).
 *   At each boot main() reads /lfs/boot_count, increments it, writes it back,
 *   appends a line to /lfs/log.txt and prints the files.
 *   Shell: fs ls /lfs, fs cat /lfs/log.txt, fs write ..., fs rm ...
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

#define STACK_SIZE   1024

/* FreeRTOS: bigger number = higher priority.  Zephyr: smaller number = higher.
 * Rule used in all labs: zephyr_prio = 10 - freertos_prio  (FreeRTOS 2 -> 8). */
#define TASK_PRIO    8

#define LED_SPEC(alias) GPIO_DT_SPEC_GET_OR(DT_ALIAS(alias), gpios, {0})

static const struct gpio_dt_spec leds[4] = {
	LED_SPEC(led0), LED_SPEC(led1), LED_SPEC(led2), LED_SPEC(led3),
};

/* One stack + one thread object per task (FreeRTOS allocates them in the heap) */
K_THREAD_STACK_DEFINE(stack1, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack2, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack3, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack4, STACK_SIZE);
static struct k_thread thread1, thread2, thread3, thread4;

static void led_toggle(int i)
{
	if (leds[i].port != NULL) {
		gpio_pin_toggle_dt(&leds[i]);
	}
}

static void leds_init(void)
{
	for (int i = 0; i < ARRAY_SIZE(leds); i++) {
		if (leds[i].port != NULL && gpio_is_ready_dt(&leds[i])) {
			gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		}
	}
}

/* Periodic task */
static void task1(void *p1, void *p2, void *p3)
{
   uint32_t counter=0;

	while (1) {
		printk("Task 1 is running....\n");
		led_toggle(0);
		counter++;
		if (counter==10)
		{
			
			return ;
		}
		k_msleep(1000);
	}
}

/* Periodic task */
static void task2(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 2 is running....\n");
		led_toggle(1);
		k_msleep(500);
	}
}

/* Periodic task */
static void task3(void *p1, void *p2, void *p3)
{
	while (1) {
		printk("Task 3 is running....\n");
		led_toggle(2);
		k_msleep(300);
	}
}

/* Periodic task */
static void task4(void *p1, void *p2, void *p3)
{
	while (1) {
	//	printk("Task 4 is running....\n");
		led_toggle(3);
		k_msleep(700);

		
	}
}

/* ------------------------------------------------------------- littlefs */

#define MNT_POINT        "/lfs"
#define BOOT_COUNT_FILE  MNT_POINT "/boot_count"
#define LOG_FILE         MNT_POINT "/log.txt"

/* littlefs config (caches, block sizes from Kconfig) + mount description */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);
static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = MNT_POINT,
};

/* Mount /lfs. The first time (blank flash) littlefs formats the partition:
 * on STM32F4 this erases 128 KB sectors and the CPU stalls ~1-2 s each. */
static int lfs_init(void)
{
	struct fs_statvfs st;
	int rc;

	rc = fs_mount(&lfs_mnt);
	if (rc < 0) {
		printk("littlefs: mount failed (%d)\n", rc);
		return rc;
	}

	fs_statvfs(MNT_POINT, &st);
	printk("littlefs: %s mounted, %lu blocks of %lu bytes, %lu free\n",
	       MNT_POINT, st.f_blocks, st.f_frsize, st.f_bfree);
	return 0;
}

/* Read a uint32_t from the file (0 if it does not exist yet), add 1, write it back */
static uint32_t boot_count_update(void)
{
	struct fs_file_t file;
	uint32_t count = 0;
	ssize_t n;

	fs_file_t_init(&file);
	if (fs_open(&file, BOOT_COUNT_FILE, FS_O_CREATE | FS_O_RDWR) < 0) {
		printk("littlefs: cannot open %s\n", BOOT_COUNT_FILE);
		return 0;
	}

	n = fs_read(&file, &count, sizeof(count));
	if (n != sizeof(count)) {
		count = 0;		/* new (empty) file */
	}
	count++;

	fs_seek(&file, 0, FS_SEEK_SET);
	fs_write(&file, &count, sizeof(count));
	fs_close(&file);	/* data is really on flash only after close/sync */

	return count;
}

/* Append one text line at the end of the file */
static void log_append(const char *line)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	if (fs_open(&file, LOG_FILE, FS_O_CREATE | FS_O_APPEND | FS_O_WRITE) < 0) {
		printk("littlefs: cannot open %s\n", LOG_FILE);
		return;
	}
	fs_write(&file, line, strlen(line));
	fs_close(&file);
}

/* Print the whole text file */
static void file_print(const char *path)
{
	struct fs_file_t file;
	char buf[64];
	ssize_t n;

	fs_file_t_init(&file);
	if (fs_open(&file, path, FS_O_READ) < 0) {
		printk("littlefs: cannot open %s\n", path);
		return;
	}

	printk("----- %s -----\n", path);
	while ((n = fs_read(&file, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		printk("%s", buf);
	}
	printk("----------------------\n");
	fs_close(&file);
}

/* List the files of a directory with their size */
static void dir_list(const char *path)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, path) < 0) {
		return;
	}
	while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
		printk("  %s %-12s %zu bytes\n",
		       entry.type == FS_DIR_ENTRY_DIR ? "[DIR]" : "     ",
		       entry.name, entry.size);
	}
	fs_closedir(&dir);
}

static void lfs_demo(void)
{
	char line[48];
	uint32_t boot;

	if (lfs_init() < 0) {
		return;
	}

	boot = boot_count_update();
	printk("littlefs: boot number %u\n", boot);

	snprintk(line, sizeof(line), "boot %u, uptime %lld ms\n", boot, k_uptime_get());
	log_append(line);

	printk("Files in %s:\n", MNT_POINT);
	dir_list(MNT_POINT);
	file_print(LOG_FILE);
}

/* ----------------------------------------------------------------- main */

int main(void)
{
	printk("LAB RTOS (Zephyr) ------------------- \n");

	leds_init();

	/* Before the tasks start: a flash erase freezes the CPU */
	lfs_demo();


	k_thread_create(&thread4, stack4, STACK_SIZE, task4, NULL, NULL, NULL,
			TASK_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread4, "T4");


	//join thread1 to wait for its completion before exiting main
	k_thread_join(&thread1, K_FOREVER);
    printk("Task 1 has completed. Exiting main...\n");

	/* No vTaskStartScheduler(): returning from main() is allowed in Zephyr. */
	return 0;
}
