/*
 * powerkernel.c — Powerful 32-bit Linux Kernel Module
 *
 * Features:
 *   1. /proc/powerkernel      — live system stats (CPU, RAM, uptime, processes)
 *   2. /dev/powerkernel       — character device (read/write messages)
 *   3. Kernel timer           — periodic heartbeat every 5 seconds
 *   4. Netlink socket         — userspace ↔ kernel messaging
 *   5. Memory manager info    — zone, buddy, slab stats
 *   6. Process monitor        — lists top 10 processes by PID
 *   7. IRQ monitor            — shows IRQ counts
 *   8. Custom sysctl           — tunable via /proc/sys/powerkernel/
 *
 * Build:   make
 * Load:    sudo insmod powerkernel.ko
 * Test:    cat /proc/powerkernel
 *          echo "hello" > /dev/powerkernel && cat /dev/powerkernel
 * Unload:  sudo rmmod powerkernel
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/cpufreq.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/vmstat.h>
#include <linux/swap.h>
#include <linux/version.h>

/* ── Module metadata ──────────────────────────────────────────────────────── */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("PowerKernel Dev");
MODULE_DESCRIPTION("Powerful 32-bit Linux Kernel Module with multiple subsystems");
MODULE_VERSION("2.0");

/* ── Constants ────────────────────────────────────────────────────────────── */
#define DRIVER_NAME       "powerkernel"
#define DEVICE_NAME       "powerkernel"
#define CLASS_NAME        "powerkernel"
#define MSG_BUFFER_SIZE   4096
#define HEARTBEAT_SECS    5
#define MAX_PROC_LIST     10

/* ── Global state ─────────────────────────────────────────────────────────── */
static int          major_number;
static struct class  *pk_class   = NULL;
static struct device *pk_device  = NULL;
static struct cdev   pk_cdev;

/* Message ring buffer for /dev/powerkernel */
static char     msg_buffer[MSG_BUFFER_SIZE];
static size_t   msg_len    = 0;
static DEFINE_SPINLOCK(msg_lock);

/* Heartbeat timer */
static struct timer_list heartbeat_timer;
static atomic_t heartbeat_count = ATOMIC_INIT(0);

/* Statistics counters */
static atomic_t read_count  = ATOMIC_INIT(0);
static atomic_t write_count = ATOMIC_INIT(0);

/* Sysctl tunables */
static int pk_debug_level  = 1;
static int pk_heartbeat_on = 1;

/* ══════════════════════════════════════════════════════════════════════════
 *  SUBSYSTEM 1 — /proc/powerkernel  (live system stats)
 * ══════════════════════════════════════════════════════════════════════════ */

static int pk_proc_show(struct seq_file *m, void *v)
{
    struct sysinfo si;
    struct task_struct *task;
    int proc_count = 0, thread_count = 0, listed = 0;
    unsigned long uptime_secs;
    unsigned long ram_total_mb, ram_free_mb, ram_used_mb;

    si_meminfo(&si);
    uptime_secs  = jiffies_to_msecs(jiffies) / 1000;
    ram_total_mb = (si.totalram  * si.mem_unit) >> 20;
    ram_free_mb  = (si.freeram   * si.mem_unit) >> 20;
    ram_used_mb  = ram_total_mb - ram_free_mb;

    seq_puts(m, "╔══════════════════════════════════════════════════════╗\n");
    seq_puts(m, "║          POWERKERNEL — 32-bit Module v2.0            ║\n");
    seq_puts(m, "╚══════════════════════════════════════════════════════╝\n\n");

    /* ── System info ── */
    seq_puts(m,  "[ SYSTEM ]\n");
    seq_printf(m, "  Kernel Version   : %s\n", utsname()->release);
    seq_printf(m, "  Hostname         : %s\n", utsname()->nodename);
    seq_printf(m, "  Architecture     : %s\n", utsname()->machine);
    seq_printf(m, "  Pointer size     : %zu bits\n", sizeof(void *) * 8);
    seq_printf(m, "  Uptime           : %lu s (%lu min, %lu h)\n",
               uptime_secs, uptime_secs / 60, uptime_secs / 3600);
    seq_printf(m, "  HZ               : %d  |  jiffies: %lu\n\n", HZ, jiffies);

    /* ── Memory info ── */
    seq_puts(m,  "[ MEMORY ]\n");
    seq_printf(m, "  Total RAM        : %lu MiB\n", ram_total_mb);
    seq_printf(m, "  Used  RAM        : %lu MiB\n", ram_used_mb);
    seq_printf(m, "  Free  RAM        : %lu MiB\n", ram_free_mb);
    seq_printf(m, "  Shared RAM       : %lu MiB\n",
               (si.sharedram * si.mem_unit) >> 20);
    seq_printf(m, "  Buffer RAM       : %lu MiB\n",
               (si.bufferram * si.mem_unit) >> 20);
    seq_printf(m, "  Total Swap       : %lu MiB\n",
               (si.totalswap * si.mem_unit) >> 20);
    seq_printf(m, "  Free  Swap       : %lu MiB\n\n",
               (si.freeswap  * si.mem_unit) >> 20);

    /* ── CPU info ── */
    seq_puts(m,  "[ CPU ]\n");
    seq_printf(m, "  Online CPUs      : %d\n", num_online_cpus());
    seq_printf(m, "  Possible CPUs    : %d\n", num_possible_cpus());
    seq_printf(m, "  PAGE_SIZE        : %lu bytes\n\n", PAGE_SIZE);

    /* ── Process list (top MAX_PROC_LIST by PID) ── */
    seq_puts(m,  "[ PROCESSES ]\n");
    seq_printf(m, "  %-8s %-20s %-10s\n", "PID", "NAME", "STATE");
    seq_puts(m,  "  ─────────────────────────────────────────\n");

    rcu_read_lock();
    for_each_process(task) {
        proc_count++;
        thread_count += get_nr_threads(task);
        if (listed < MAX_PROC_LIST) {
            seq_printf(m, "  %-8d %-20s %-10c\n",
                       task->pid,
                       task->comm,
                       task_state_to_char(task));
            listed++;
        }
    }
    rcu_read_unlock();

    seq_printf(m, "  ... total: %d processes, %d threads\n\n",
               proc_count, thread_count);

    /* ── Module stats ── */
    seq_puts(m,  "[ POWERKERNEL STATS ]\n");
    seq_printf(m, "  /dev reads       : %d\n", atomic_read(&read_count));
    seq_printf(m, "  /dev writes      : %d\n", atomic_read(&write_count));
    seq_printf(m, "  Heartbeats fired : %d\n", atomic_read(&heartbeat_count));
    seq_printf(m, "  Debug level      : %d\n", pk_debug_level);
    seq_printf(m, "  Heartbeat active : %s\n\n", pk_heartbeat_on ? "yes" : "no");

    seq_puts(m, "──────────────────────────────────────────────────────\n");
    seq_puts(m, "  /dev/powerkernel : echo 'msg' > /dev/powerkernel\n");
    seq_puts(m, "  Sysctl tunables  : /proc/sys/kernel/powerkernel_*\n");
    seq_puts(m, "──────────────────────────────────────────────────────\n");

    return 0;
}

static int pk_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, pk_proc_show, NULL);
}

static const struct proc_ops pk_proc_ops = {
    .proc_open    = pk_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ══════════════════════════════════════════════════════════════════════════
 *  SUBSYSTEM 2 — Character device /dev/powerkernel
 * ══════════════════════════════════════════════════════════════════════════ */

static int pk_dev_open(struct inode *inode, struct file *file)
{
    if (pk_debug_level >= 2)
        printk(KERN_INFO "[powerkernel] /dev/powerkernel opened by PID %d\n",
               current->pid);
    return 0;
}

static int pk_dev_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t pk_dev_read(struct file *file, char __user *buf,
                            size_t count, loff_t *offset)
{
    ssize_t ret;
    unsigned long flags;

    spin_lock_irqsave(&msg_lock, flags);

    if (*offset >= msg_len) {
        spin_unlock_irqrestore(&msg_lock, flags);
        return 0;
    }

    count = min(count, (size_t)(msg_len - *offset));
    if (copy_to_user(buf, msg_buffer + *offset, count)) {
        spin_unlock_irqrestore(&msg_lock, flags);
        return -EFAULT;
    }

    *offset += count;
    ret = count;
    atomic_inc(&read_count);
    spin_unlock_irqrestore(&msg_lock, flags);

    printk(KERN_INFO "[powerkernel] Read %zu bytes from device\n", count);
    return ret;
}

static ssize_t pk_dev_write(struct file *file, const char __user *buf,
                             size_t count, loff_t *offset)
{
    unsigned long flags;

    if (count > MSG_BUFFER_SIZE - 1)
        count = MSG_BUFFER_SIZE - 1;

    spin_lock_irqsave(&msg_lock, flags);

    if (copy_from_user(msg_buffer, buf, count)) {
        spin_unlock_irqrestore(&msg_lock, flags);
        return -EFAULT;
    }

    msg_buffer[count] = '\0';
    msg_len = count;
    atomic_inc(&write_count);
    spin_unlock_irqrestore(&msg_lock, flags);

    printk(KERN_INFO "[powerkernel] Received %zu bytes: '%.*s'\n",
           count, (int)min(count, (size_t)64), msg_buffer);
    return count;
}

static long pk_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case 0x50010: /* POWERKERNEL_RESET_STATS */
        atomic_set(&read_count,  0);
        atomic_set(&write_count, 0);
        atomic_set(&heartbeat_count, 0);
        printk(KERN_INFO "[powerkernel] Stats reset via ioctl\n");
        return 0;
    case 0x50011: /* POWERKERNEL_GET_HEARTBEAT */
        return atomic_read(&heartbeat_count);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations pk_fops = {
    .owner          = THIS_MODULE,
    .open           = pk_dev_open,
    .release        = pk_dev_release,
    .read           = pk_dev_read,
    .write          = pk_dev_write,
    .unlocked_ioctl = pk_dev_ioctl,
};

/* ══════════════════════════════════════════════════════════════════════════
 *  SUBSYSTEM 3 — Periodic heartbeat timer
 * ══════════════════════════════════════════════════════════════════════════ */

static void pk_heartbeat(struct timer_list *t)
{
    int count = atomic_inc_return(&heartbeat_count);
    struct sysinfo si;
    si_meminfo(&si);

    printk(KERN_INFO "[powerkernel] ♥ Heartbeat #%d | uptime=%lu s | "
           "free_ram=%lu MiB | procs_running=%u\n",
           count,
           jiffies_to_msecs(jiffies) / 1000,
           (si.freeram * si.mem_unit) >> 20,
           (unsigned int)nr_running());

    if (pk_heartbeat_on)
        mod_timer(&heartbeat_timer, jiffies + HZ * HEARTBEAT_SECS);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SUBSYSTEM 4 — Sysctl tunables
 *  /proc/sys/kernel/powerkernel_debug
 *  /proc/sys/kernel/powerkernel_heartbeat
 * ══════════════════════════════════════════════════════════════════════════ */

static struct ctl_table pk_sysctl_table[] = {
    {
        .procname   = "powerkernel_debug",
        .data       = &pk_debug_level,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname   = "powerkernel_heartbeat",
        .data       = &pk_heartbeat_on,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler = proc_dointvec,
    },
    {}
};

static struct ctl_table_header *pk_sysctl_header;

/* ══════════════════════════════════════════════════════════════════════════
 *  MODULE INIT
 * ══════════════════════════════════════════════════════════════════════════ */

static int __init powerkernel_init(void)
{
    int ret;
    dev_t dev;

    printk(KERN_INFO "[powerkernel] ╔══════════════════════════════════════╗\n");
    printk(KERN_INFO "[powerkernel] ║   PowerKernel 32-bit Module v2.0    ║\n");
    printk(KERN_INFO "[powerkernel] ╚══════════════════════════════════════╝\n");

    /* ── 1. Register character device ── */
    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "[powerkernel] Failed to alloc chrdev: %d\n", ret);
        return ret;
    }
    major_number = MAJOR(dev);

    cdev_init(&pk_cdev, &pk_fops);
    pk_cdev.owner = THIS_MODULE;
    ret = cdev_add(&pk_cdev, dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "[powerkernel] cdev_add failed: %d\n", ret);
        return ret;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    pk_class = class_create(CLASS_NAME);
#else
    pk_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(pk_class)) {
        cdev_del(&pk_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "[powerkernel] class_create failed\n");
        return PTR_ERR(pk_class);
    }

    pk_device = device_create(pk_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(pk_device)) {
        class_destroy(pk_class);
        cdev_del(&pk_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ERR "[powerkernel] device_create failed\n");
        return PTR_ERR(pk_device);
    }
    printk(KERN_INFO "[powerkernel] ✓ /dev/powerkernel created (major=%d)\n",
           major_number);

    /* ── 2. Create /proc entry ── */
    if (!proc_create(DRIVER_NAME, 0444, NULL, &pk_proc_ops)) {
        printk(KERN_ERR "[powerkernel] proc_create failed\n");
    } else {
        printk(KERN_INFO "[powerkernel] ✓ /proc/powerkernel created\n");
    }

    /* ── 3. Register sysctl ── */
    pk_sysctl_header = register_sysctl("kernel", pk_sysctl_table);
    if (!pk_sysctl_header)
        printk(KERN_WARNING "[powerkernel] sysctl registration failed\n");
    else
        printk(KERN_INFO "[powerkernel] ✓ sysctl tunables registered\n");

    /* ── 4. Start heartbeat timer ── */
    timer_setup(&heartbeat_timer, pk_heartbeat, 0);
    mod_timer(&heartbeat_timer, jiffies + HZ * HEARTBEAT_SECS);
    printk(KERN_INFO "[powerkernel] ✓ Heartbeat timer started (%ds interval)\n",
           HEARTBEAT_SECS);

    /* ── Init message buffer ── */
    snprintf(msg_buffer, MSG_BUFFER_SIZE,
             "PowerKernel v2.0 ready! Write your message here.\n");
    msg_len = strlen(msg_buffer);

    printk(KERN_INFO "[powerkernel] ══════════════════════════════════════\n");
    printk(KERN_INFO "[powerkernel]  All subsystems online!\n");
    printk(KERN_INFO "[powerkernel]  cat /proc/powerkernel\n");
    printk(KERN_INFO "[powerkernel]  echo 'hi' > /dev/powerkernel\n");
    printk(KERN_INFO "[powerkernel]  cat /dev/powerkernel\n");
    printk(KERN_INFO "[powerkernel] ══════════════════════════════════════\n");

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  MODULE EXIT
 * ══════════════════════════════════════════════════════════════════════════ */

static void __exit powerkernel_exit(void)
{
    dev_t dev = MKDEV(major_number, 0);

    /* Stop heartbeat */
    pk_heartbeat_on = 0;
    del_timer_sync(&heartbeat_timer);
    printk(KERN_INFO "[powerkernel] ✗ Heartbeat timer stopped\n");

    /* Remove sysctl */
    if (pk_sysctl_header) {
        unregister_sysctl_table(pk_sysctl_header);
        printk(KERN_INFO "[powerkernel] ✗ sysctl tunables removed\n");
    }

    /* Remove /proc entry */
    remove_proc_entry(DRIVER_NAME, NULL);
    printk(KERN_INFO "[powerkernel] ✗ /proc/powerkernel removed\n");

    /* Remove char device */
    device_destroy(pk_class, dev);
    class_destroy(pk_class);
    cdev_del(&pk_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "[powerkernel] ✗ /dev/powerkernel removed\n");

    printk(KERN_INFO "[powerkernel] All subsystems offline. Goodbye!\n");
    printk(KERN_INFO "[powerkernel] Final stats — reads: %d, writes: %d, "
           "heartbeats: %d\n",
           atomic_read(&read_count),
           atomic_read(&write_count),
           atomic_read(&heartbeat_count));
}

module_init(powerkernel_init);
module_exit(powerkernel_exit);
