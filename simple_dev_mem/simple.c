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


#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/raw.h>
#include <linux/tty.h>
#include <linux/capability.h>
#include <linux/ptrace.h>
#include <linux/highmem.h>
#include <linux/backing-dev.h>
#include <linux/splice.h>
#include <linux/pfn.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/uio.h>

#include <linux/uaccess.h>
#include <linux/device.h>

static unsigned char *myaddr_fault = NULL;
static unsigned char *myaddr_remap = NULL;

static int simple_major = 0;
module_param(simple_major, int, 0);
MODULE_AUTHOR("Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

static int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
    return 1;
}

static int private_mapping_ok(struct vm_area_struct *vma)
{
    return vma->vm_flags & VM_MAYSHARE;
}

static int range_is_allowed(unsigned long pfn, unsigned long size)
{
    return 1;
}

int __weak phys_mem_access_prot_allowed(struct file *file,
    unsigned long pfn, unsigned long size, pgprot_t *vma_prot)
{
    return 1;
}

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
                     unsigned long size, pgprot_t vma_prot)
{
    return vma_prot;
}

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

static int simple_remap_mmap(struct file *filp, struct vm_area_struct *vma)
{
    size_t size;
    size = vma->vm_end - vma->vm_start;
    printk (KERN_NOTICE "########### CALL remap_mmap");

    if (!valid_mmap_phys_addr_range(vma->vm_pgoff, size))
        return -EINVAL;

    if (!private_mapping_ok(vma))
        return -ENOSYS;

    if (!range_is_allowed(vma->vm_pgoff, size))
        return -EPERM;

    if (!phys_mem_access_prot_allowed(filp, vma->vm_pgoff, size,
                        &vma->vm_page_prot))
        return -EINVAL;

    vma->vm_page_prot = phys_mem_access_prot(filp, vma->vm_pgoff,
                         size,
                         vma->vm_page_prot);

    vma->vm_pgoff = __pa(myaddr_remap)>>PAGE_SHIFT;

    /* Remap-pfn-range will mark the range VM_IO */
    if (remap_pfn_range(vma,
                vma->vm_start,
                vma->vm_pgoff,
                size,
                vma->vm_page_prot)) {
        return -EAGAIN;
    }
	vma->vm_ops = &simple_remap_vm_ops;
	simple_vma_open(vma);

	return 0;
}



/*
 * The fault version.
 */
int simple_vma_fault(struct vm_area_struct *vma,
                struct vm_fault *vmf)
{
	struct page *pageptr;
    	printk (KERN_NOTICE "########### CALL fault_mmap");

	unsigned long offset = vmf->virtual_address - vma->vm_start;
	pageptr = virt_to_page(myaddr_fault);

	printk (KERN_NOTICE "page->index = %ld mapping %p\n", pageptr->index, pageptr->mapping);
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

	myaddr_fault = __get_free_pages(GFP_KERNEL, 1);	
	myaddr_remap = __get_free_pages(GFP_KERNEL, 1);	

	// content for test
	strcpy(myaddr_fault, "00000000, fault version");
	strcpy(myaddr_remap, "11111111, remap version");

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
