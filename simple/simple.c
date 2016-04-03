/*
 * Simple - REALLY simple memory mapping demonstration.
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: simple.c,v 1.12 2005/01/31 16:15:31 rubini Exp $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <asm/page.h>
#include <linux/cdev.h>

#include <linux/device.h>

static int simple_major = 0;
module_param(simple_major, int, 0);
MODULE_AUTHOR("Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * Open the device; in fact, there's nothing to do here.
 */
static int ldd_simple_open (struct inode *inode, struct file *filp)
{
	return 0;
}


/*
 * Closing is just as simpler.
 */
static int simple_release(struct inode *inode, struct file *filp)
{
	return 0;
}



/*
 * Common VMA ops.
 */

void simple_vma_open(struct vm_area_struct *vma)
{
	printk(KERN_NOTICE "Simple VMA open, virt %lx, phys %lx\n",
			vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

void simple_vma_close(struct vm_area_struct *vma)
{
	printk(KERN_NOTICE "Simple VMA close.\n");
}


/*
 * The remap_pfn_range version of mmap.  This one is heavily borrowed
 * from drivers/char/mem.c.
 */

static struct vm_operations_struct simple_remap_vm_ops = {
	.open =  simple_vma_open,
	.close = simple_vma_close,
};

/*
 * 第三个参数直接使用vma->vm_pgoff，
 * 是因为这部分代码是直接从drivers/char/mem.c中拷贝而来，所以它模拟的设备工作方式也类似/dev/mem，
 * 而/dev/mem的pgoff刚好等于pgn。
 *
 * 1.在进程的虚拟空间查找一块VMA.
 * 2.将这块VMA进行映射.
 * 3.如果设备驱动程序中定义了mmap函数,则调用它.
 * 4.将这个VMA插入到进程的VMA链表中.
 * 内存映射工作大部分由内核完成，驱动程序中的mmap函数只需要为该地址范围建立合适的页表，
 * 并将vma->vm_ops替换为一系列的新操作就可以了。
 *
 * virt_addr代表要建立页表的用户虚拟地址的起始地址，remap_pfn_range函数为处于virt_addr和virt_addr+size之间的虚拟地址建立页表。
 * pfn是与物理内存起始地址对应的页帧号，虚拟内存将要被映射到该物理内存上。
 * 页帧号只是将物理地址右移PAGE_SHIFT位。在大多数情况下，VMA结构中的vm_pgoff赋值给pfn即可。
 * remap_pfn_range函数建立页表，对应的物理地址是pfn<<PAGE_SHIFT到pfn<<(PAGE_SHIFT)+size。
 * size代表虚拟内存区域大小。
 */

static int simple_remap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start,
			    vma->vm_page_prot))
		return -EAGAIN;

	vma->vm_ops = &simple_remap_vm_ops;
	simple_vma_open(vma);
	return 0;
}



/*
 * The fault version.
 */
/*
 * 每次只映射一个PAGE
 * 1.找到缺页的虚拟地址所在的VMA。
 * 2.如果必要，分配中间页目录表和页表。
 * 3.如果页表项对应的物理页面不存在，则调用nopage函数，它返回物理页面的页描述符。
 * 4.将物理页面的地址填充到页表中。
 * 在上面第3步中，分配好的页目录和页表直接对应的是物理PAGE，
 * 如果在不修改页表映射的情况下是这样，如果修改了就是别的PAGE物理地址。
 * 下面的代码拿到的物理地址没有经过修改，所以是直接映射到物理内存的。
 * 和上面的remap的行为一直，都是直接映射虚拟地址对应的物理地址。
 * 和/dev/mem一致。
 */
int simple_vma_fault(struct vm_area_struct *vma,
                struct vm_fault *vmf)
{
	struct page *pageptr;
    // 得到起始物理地址保存在offset中
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    // 得到vmf->virtual_address对应的物理地址，保存在physaddr中
	unsigned long physaddr = (unsigned long)(vmf->virtual_address - vma->vm_start) + offset;
    // 得到物理地址对应的页帧号，保存在pageframe中。
	unsigned long pageframe = physaddr >> PAGE_SHIFT;

// Eventually remove these printks
	printk (KERN_NOTICE "---- fault, off %lx phys %lx\n", offset, physaddr);
	printk (KERN_NOTICE "VA is %p\n", __va (physaddr));
	printk (KERN_NOTICE "Page at %p\n", virt_to_page (__va (physaddr)));
	if (!pfn_valid(pageframe))
		return VM_FAULT_SIGBUS;
    // 由页帧号返回对应的page结构指针保存在pageptr中
	pageptr = pfn_to_page(pageframe);
	printk (KERN_NOTICE "page->index = %ld mapping %p\n", pageptr->index, pageptr->mapping);
	printk (KERN_NOTICE "Page frame %ld\n", pageframe);
    // 调用get_page增加pageptr指向页面的引用计数
	get_page(pageptr);
        vmf->page = pageptr;
	return 0;
}

static struct vm_operations_struct simple_fault_vm_ops = {
	.open =   simple_vma_open,
	.close =  simple_vma_close,
	.fault = simple_vma_fault,
};

static int simple_fault_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset >= __pa(high_memory) || (filp->f_flags & O_SYNC))
		vma->vm_flags |= VM_IO;
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);

	vma->vm_ops = &simple_fault_vm_ops;
	simple_vma_open(vma);
	return 0;
}


/*
 * Set up the cdev structure for a device.
 */
static void simple_setup_cdev(struct cdev *dev, int minor,
		struct file_operations *fops)
{
	int err, devno = MKDEV(simple_major, minor);
    
	cdev_init(dev, fops);
	dev->owner = THIS_MODULE;
	dev->ops = fops;
	err = cdev_add (dev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk (KERN_NOTICE "Error %d adding simple%d", err, minor);
}


/*
 * Our various sub-devices.
 */
/* Device 0 uses remap_pfn_range */
static struct file_operations simple_remap_ops = {
	.owner   = THIS_MODULE,
	.open    = ldd_simple_open,
	.release = simple_release,
	.mmap    = simple_remap_mmap,
};

/* Device 1 uses fault */
static struct file_operations simple_fault_ops = {
	.owner   = THIS_MODULE,
	.open    = ldd_simple_open,
	.release = simple_release,
	.mmap    = simple_fault_mmap,
};

#define MAX_SIMPLE_DEV 2

#if 0
static struct file_operations *simple_fops[MAX_SIMPLE_DEV] = {
	&simple_remap_ops,
	&simple_fault_ops,
};
#endif

/*
 * We export two simple devices.  There's no need for us to maintain any
 * special housekeeping info, so we just deal with raw cdevs.
 */
static struct cdev SimpleDevs[MAX_SIMPLE_DEV];

/*
 * Module housekeeping.
 */
static int simple_init(void)
{
	int result;
	dev_t dev = MKDEV(simple_major, 0);

	/* Figure out our device number. */
	if (simple_major)
		result = register_chrdev_region(dev, 2, "simple");
	else {
		result = alloc_chrdev_region(&dev, 0, 2, "simple");
		simple_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "simple: unable to get major %d\n", simple_major);
		return result;
	}
	if (simple_major == 0)
		simple_major = result;

	/* Now set up two cdevs. */
	simple_setup_cdev(SimpleDevs, 0, &simple_remap_ops);
	simple_setup_cdev(SimpleDevs + 1, 1, &simple_fault_ops);
	return 0;
}


static void simple_cleanup(void)
{
	cdev_del(SimpleDevs);
	cdev_del(SimpleDevs + 1);
	unregister_chrdev_region(MKDEV(simple_major, 0), 2);
}


module_init(simple_init);
module_exit(simple_cleanup);
