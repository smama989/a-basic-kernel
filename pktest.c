/*
 * pktest.c — Userspace test program for PowerKernel module
 *
 * Compile:  gcc -o pktest pktest.c
 * Run:      sudo ./pktest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>

#define DEVICE_PATH  "/dev/powerkernel"
#define PROC_PATH    "/proc/powerkernel"
#define BUF_SIZE     4096

/* IOCTL commands (must match kernel module) */
#define PK_RESET_STATS    0x50010
#define PK_GET_HEARTBEAT  0x50011

/* ── Colour helpers ──────────────────────────────────────────────────────── */
#define RED    "\033[31m"
#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define CYAN   "\033[36m"
#define BOLD   "\033[1m"
#define RESET  "\033[0m"

static void banner(void)
{
    printf(BOLD CYAN);
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║     PowerKernel Userspace Test Tool          ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf(RESET);
}

/* ── Test 1: Read /proc/powerkernel ──────────────────────────────────────── */
static void test_proc(void)
{
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    printf(BOLD "\n[TEST 1] Reading /proc/powerkernel\n" RESET);
    fd = open(PROC_PATH, O_RDONLY);
    if (fd < 0) {
        printf(RED "  FAIL: cannot open %s — %s\n" RESET, PROC_PATH, strerror(errno));
        printf(YELLOW "  Hint: is the module loaded? (sudo insmod powerkernel.ko)\n" RESET);
        return;
    }

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(fd);
    printf(GREEN "  ✓ /proc read OK\n" RESET);
}

/* ── Test 2: Write to /dev/powerkernel ───────────────────────────────────── */
static void test_write(const char *msg)
{
    int fd;
    ssize_t n;

    printf(BOLD "\n[TEST 2] Writing to /dev/powerkernel\n" RESET);
    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        printf(RED "  FAIL: cannot open %s for write — %s\n" RESET,
               DEVICE_PATH, strerror(errno));
        return;
    }

    n = write(fd, msg, strlen(msg));
    close(fd);

    if (n < 0)
        printf(RED "  FAIL: write error — %s\n" RESET, strerror(errno));
    else
        printf(GREEN "  ✓ Wrote %zd bytes: \"%s\"\n" RESET, n, msg);
}

/* ── Test 3: Read from /dev/powerkernel ──────────────────────────────────── */
static void test_read(void)
{
    char buf[BUF_SIZE];
    ssize_t n;
    int fd;

    printf(BOLD "\n[TEST 3] Reading from /dev/powerkernel\n" RESET);
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        printf(RED "  FAIL: cannot open %s for read — %s\n" RESET,
               DEVICE_PATH, strerror(errno));
        return;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0)
        printf(RED "  FAIL: read error — %s\n" RESET, strerror(errno));
    else {
        buf[n] = '\0';
        printf(GREEN "  ✓ Read %zd bytes:\n" RESET, n);
        printf("  \"%s\"\n", buf);
    }
}

/* ── Test 4: IOCTL ───────────────────────────────────────────────────────── */
static void test_ioctl(void)
{
    int fd;
    long hb;

    printf(BOLD "\n[TEST 4] IOCTL — get heartbeat count\n" RESET);
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        printf(RED "  FAIL: cannot open device — %s\n" RESET, strerror(errno));
        return;
    }

    hb = ioctl(fd, PK_GET_HEARTBEAT);
    if (hb < 0)
        printf(RED "  FAIL: ioctl PK_GET_HEARTBEAT — %s\n" RESET, strerror(errno));
    else
        printf(GREEN "  ✓ Heartbeat count: %ld\n" RESET, hb);

    printf("  Resetting stats via ioctl...\n");
    if (ioctl(fd, PK_RESET_STATS) == 0)
        printf(GREEN "  ✓ Stats reset OK\n" RESET);
    else
        printf(YELLOW "  Stats reset returned error (may need root)\n" RESET);

    close(fd);
}

/* ── Test 5: Stress write + read loop ────────────────────────────────────── */
static void test_stress(int iterations)
{
    char msg[128];
    char buf[BUF_SIZE];
    int fd_w, fd_r, i;
    ssize_t n;

    printf(BOLD "\n[TEST 5] Stress test (%d iterations)\n" RESET, iterations);

    for (i = 0; i < iterations; i++) {
        snprintf(msg, sizeof(msg), "stress-test iteration %d at time %ld", i, time(NULL));

        fd_w = open(DEVICE_PATH, O_WRONLY);
        if (fd_w < 0) { printf(RED "  FAIL at iter %d write\n" RESET, i); break; }
        write(fd_w, msg, strlen(msg));
        close(fd_w);

        fd_r = open(DEVICE_PATH, O_RDONLY);
        if (fd_r < 0) { printf(RED "  FAIL at iter %d read\n" RESET, i); break; }
        n = read(fd_r, buf, sizeof(buf) - 1);
        close(fd_r);

        if (n > 0 && strncmp(buf, msg, strlen(msg)) == 0)
            printf("  iter %3d " GREEN "✓\n" RESET, i);
        else
            printf("  iter %3d " RED "✗ mismatch\n" RESET, i);
    }
    printf(GREEN "  ✓ Stress test complete\n" RESET);
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    banner();

    test_proc();
    test_write("Hello PowerKernel from userspace!");
    test_read();
    test_ioctl();
    test_stress(5);

    printf(BOLD CYAN "\n[ All tests done ]\n" RESET);
    printf("Check kernel log:  sudo dmesg | grep powerkernel | tail -30\n\n");
    return 0;
}
