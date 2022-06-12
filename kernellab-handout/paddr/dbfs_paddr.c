#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

#define MAX_LEN 100

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

struct packet {
    pid_t pid;
    unsigned long vaddr;
    unsigned long paddr;
};

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
    struct mm_struct *mm;
    struct packet pckt;
    char out_buf[MAX_LEN]; memset(out_buf, 0, MAX_LEN);

    pgd_t *pgdp;
    p4d_t *p4dp;
    pud_t *pudp;
    pmd_t *pmdp;
    pte_t *ptep;

    if(copy_from_user(&pckt, user_buffer, sizeof(struct packet)) != 0) {
        printk("space is inadequate\n");
    }

    // get task_struct
    task = pid_task(find_get_pid(pckt.pid), PIDTYPE_PID);
    mm = task->mm;

    // visit multi-level page table
    pgdp = pgd_offset(mm, pckt.vaddr);
    if(pgd_none(*pgdp) || pgd_bad(*pgdp)) return -EINVAL;

    p4dp = p4d_offset(pgdp, pckt.vaddr);
    if(p4d_none(*p4dp) || p4d_bad(*p4dp)) return -EINVAL;

    pudp = pud_offset(p4dp, pckt.vaddr);
    if(pud_none(*pudp) || pud_bad(*pudp)) return -EINVAL;

    pmdp = pmd_offset(pudp, pckt.vaddr);
    if(pmd_none(*pmdp) || pmd_bad(*pmdp)) return -EINVAL;

    ptep = pte_offset_kernel(pmdp, pckt.vaddr);
    if(pte_none(*ptep) || !pte_present(*ptep)) return -EINVAL;

    // get physical address
    pckt.paddr = page_to_phys(pte_page(*ptep));

    copy_to_user(user_buffer, &pckt, sizeof(struct packet));

    return length;
}

static const struct file_operations dbfs_fops = {
    .read = read_output,
};

static int __init dbfs_module_init(void)
{
    dir = debugfs_create_dir("paddr", NULL);

    if (!dir) {
            printk("Cannot create paddr dir\n");
            return -1;
    }

    // Fill in the arguments below
    output = debugfs_create_file("output", 00700, dir, NULL, &dbfs_fops);

    printk("dbfs_paddr module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
    debugfs_remove_recursive(dir);

    printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
