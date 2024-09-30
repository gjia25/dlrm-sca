#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/rwsem.h>
#include <linux/mm_types.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clarity");
MODULE_DESCRIPTION("A module to read PTEs and return pages marked accessed");

struct read_request {
    pid_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
};

#define MAX_RESULTS 1024

struct result_entry {
    unsigned long vaddr;
};

static struct result_entry results[MAX_RESULTS];
static int result_count = 0;

static ssize_t read_accessed_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos);
static ssize_t read_accessed_read(struct file *file, char __user *buffer, size_t count, loff_t *pos);

static const struct proc_ops proc_fops = {
    .proc_write = read_accessed_write,
    .proc_read = read_accessed_read,
};

static int read_accessed_pages(struct mm_struct *mm, unsigned long start, unsigned long end) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long addr;

    result_count = 0;

    for (addr = start; addr < end; addr += PAGE_SIZE) {
        pgd = pgd_offset(mm, addr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            continue;

        p4d = p4d_offset(pgd, addr);
        if (p4d_none(*p4d) || p4d_bad(*p4d))
            continue;

        pud = pud_offset(p4d, addr);
        if (pud_none(*pud) || pud_bad(*pud))
            continue;

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd))
            continue;

        pte = pte_offset_map(pmd, addr);
        if (!pte)
            continue;

        if (pte_present(*pte) && pte_young(*pte)) {
            results[result_count].vaddr = addr;
            result_count++;
            if (result_count >= MAX_RESULTS){
                printk(KERN_INFO "read_accessed: Max result count reached :(\n");
                break;
            }
        }

        pte_unmap(pte);
    }
    printk(KERN_INFO "read_accessed: Read %d results for %lx-%lx\n", result_count, start, end);
    return 0;
}

static ssize_t read_accessed_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos) {
    struct task_struct *task;
    struct mm_struct *mm;
    struct read_request req;

    if (count != sizeof(req))
        return -EINVAL;

    if (copy_from_user(&req, buffer, count))
        return -EFAULT;

    // printk(KERN_INFO "read_accessed: Received request for %lx-%lx\n", req.start_vaddr, req.end_vaddr);
    rcu_read_lock();
    task = pid_task(find_vpid(req.pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }

    mm = task->mm;
    if (!mm) {
        rcu_read_unlock();
        return -EINVAL;
    }

    down_read(&mm->mmap_lock);
    read_accessed_pages(mm, req.start_vaddr, req.end_vaddr);
    up_read(&mm->mmap_lock);
    rcu_read_unlock();
    return count;
}

static ssize_t read_accessed_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    // printk(KERN_INFO "read_accessed: count=%d, pos=%ld\n", count, *pos);
    size_t res_len = result_count * sizeof(struct result_entry);
    // printk(KERN_INFO "read_accessed: %d bytes to read (%d results)\n", res_len, result_count);
    if (*pos >= res_len){
        *pos = 0;
        // printk(KERN_INFO "read_accessed: reset offset\n");
        return 0;
    }
    if (count > res_len - *pos)
        count = res_len - *pos;

    if (copy_to_user(buffer, (char *)results + *pos, count))
        return -EFAULT;

    *pos += count;
    // printk(KERN_INFO "read_accessed: %d / %d bytes read\n", count, res_len);
    return count;
}

static int __init read_accessed_init(void) {
    proc_create("read_accessed", 0644, NULL, &proc_fops);
    printk(KERN_INFO "Initializing read_accessed module\n");
    return 0;
}

static void __exit read_accessed_exit(void) {
    printk(KERN_INFO "Preparing to exit read_accessed module\n");
    remove_proc_entry("read_accessed", NULL);
    printk(KERN_INFO "Exiting read_accessed module\n");
}

module_init(read_accessed_init);
module_exit(read_accessed_exit);