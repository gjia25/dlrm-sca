#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clarity");
MODULE_DESCRIPTION("A module to clear accessed bits of PTEs for a given address range");

static int clear_accessed_bits(struct mm_struct *mm, unsigned long start, unsigned long end)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long addr;

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

        if (!pte_none(*pte)) {
            // Clear the accessed bit
            pte_t new_pte = pte_clear_flags(*pte, _PAGE_ACCESSED);
            set_pte_at(mm, addr, pte, new_pte);
        }

        pte_unmap(pte);
    }

    flush_tlb_mm_range(mm, start, end, PAGE_SHIFT, false);

    return 0;
}

struct clear_request {
    pid_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
};

static ssize_t clear_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct clear_request req;

    if (count != sizeof(req))
        return -EINVAL;

    if (copy_from_user(&req, buffer, count))
        return -EFAULT;

    rcu_read_lock();
    task = find_task_by_vpid(req.pid);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }

    mm = task->mm;
    if (!mm) {
        rcu_read_unlock();
        return -EINVAL;
    }

    down_read(&mm->mmap_sem);
    clear_accessed_bits(mm, req.start_vaddr, req.end_vaddr);
    up_read(&mm->mmap_sem);
    rcu_read_unlock();

    return count;
}

static const struct file_operations proc_fops = {
    .owner = THIS_MODULE,
    .write = clear_write,
};

static int __init clear_accessed_bits_init(void)
{
    proc_create("clear_accessed_bits", 0666, NULL, &proc_fops);
    printk(KERN_INFO "Initializing clear accessed bits module\n");
    return 0;
}

static void __exit clear_accessed_bits_exit(void)
{
    remove_proc_entry("clear_accessed_bits", NULL);
    printk(KERN_INFO "Exiting clear accessed bits module\n");
}

module_init(clear_accessed_bits_init);
module_exit(clear_accessed_bits_exit);
