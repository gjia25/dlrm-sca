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

#include <linux/moduleparam.h> /* which will have params */ 
#include <linux/kallsyms.h> /* For sprint_symbol */ 
static unsigned long sym = 0; 
module_param(sym, ulong, 0644);

static void (*flush_tlb_mm_range_stolen)(struct mm_struct *, unsigned long, unsigned long, unsigned int, bool); 

static void *acquire_flush_tlb_mm_range(void) 
{
    const char sct_name[15] = "flush_tlb_mm_range"; 
    char symbol[40] = { 0 }; 
 
    if (sym == 0) { 
        pr_alert("For Linux v5.7+, Kprobes is the preferable way to get " 
                 "symbol.\n"); 
        pr_info("If Kprobes is absent, you have to specify the address of " 
                "flush_tlb_mm_range symbol\n"); 
        pr_info("by /boot/System.map or /proc/kallsyms, which contains all the " 
                "symbol addresses, into sym parameter.\n"); 
        return NULL; 
    } 

    sprint_symbol(symbol, sym); 

    if (!strncmp(sct_name, symbol, sizeof(sct_name) - 1)) 
        return (void *)sym; 
    return NULL; 
} 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clarity");
MODULE_DESCRIPTION("A module to clear accessed bits of PTEs for a given address range");

static int clear_accessed_bits(struct mm_struct *mm, unsigned long start, unsigned long end)
{
    struct vm_area_struct *vma;
    unsigned long addr;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    for (vma = find_vma(mm, start); vma && vma->vm_start < end; vma = vma->vm_next) {
        printk(KERN_INFO "Processing %lx-%lx (out of %lx-%lx)", vma_start, vma_end, start, end);
        unsigned long vma_start = max(vma->vm_start, start);
        unsigned long vma_end = min(vma->vm_end, end);

        flush_cache_range(vma, vma_start, vma_end);
        printk(KERN_INFO "Cleared cache range for %lx-%lx", vma_start, vma_end);

        for (addr = vma_start; addr < vma_end; addr += PAGE_SIZE) {
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

            if (!pte_none(*pte))
            {
                pte_t new_pte;
                new_pte = pte_clear_flags(*pte, _PAGE_ACCESSED);
                set_pte_at(mm, addr, pte, new_pte);
            }

            pte_unmap(pte);
        }
        flush_tlb_mm_range_stolen(mm, start, end, PAGE_SHIFT, false);
        printk(KERN_INFO "Cleared TLB range for %lx-%lx", start, end);
    }
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
    clear_accessed_bits(mm, req.start_vaddr, req.end_vaddr);
    up_read(&mm->mmap_lock);
    rcu_read_unlock();

    return count;
}

static const struct proc_ops proc_fops = {
    .proc_write = clear_write,
};

static int __init clear_accessed_bits_init(void)
{
    proc_create("clear_accessed_bits", 0666, NULL, &proc_fops);
    printk(KERN_INFO "Initializing clear accessed bits module\n");
    if (!(flush_tlb_mm_range_stolen = acquire_flush_tlb_mm_range())) 
        return -1;
    return 0;
}

static void __exit clear_accessed_bits_exit(void)
{
    remove_proc_entry("clear_accessed_bits", NULL);
    printk(KERN_INFO "Exiting clear accessed bits module\n");
}

module_init(clear_accessed_bits_init);
module_exit(clear_accessed_bits_exit);
