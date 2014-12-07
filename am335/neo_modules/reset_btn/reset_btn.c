#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h> // gpio_request(), gpio_to_irq()
#include <linux/interrupt.h> // request_irq(), irqreturn_t
#include <linux/cdev.h> // cdev, cdev_init(), cdev_add()
#include <linux/fs.h> //alloc_chrdev_region(), file_operations
#include <asm/uaccess.h> // copy_from_user()
#include <linux/slab.h> // kmalloc()
#include <linux/proc_fs.h> // create_proc_entry()
//#include <linux/moduleparam.h>
//#include <linux/kernel.h>
//#include <linux/irq.h>
//#include <linux/types.h>
//#include <linux/errno.h>
//#include <linux/ioport.h>
//#include <asm/io.h>
//#include <linux/sched.h>
//#include <linux/fs.h>
//#include <linux/input.h>

#define MOD_LICENSE "GPL"
#define MOD_AUTHOR "Neo <iamhahar@gmail.com>"
#define MOD_DESCRIPTION "Simple gpio button device driver"
#define MODULE_NAME "gpio_button_driver"

MODULE_LICENSE (MOD_LICENSE);
MODULE_DESCRIPTION (MOD_DESCRIPTION);
MODULE_AUTHOR (MOD_AUTHOR);

#define GPIO_TO_PIN(bank, gpio) (32 * (bank) + (gpio))  
#define RESET_BTN_GPIO	GPIO_TO_PIN(0,17)
#define RESET_BTN_GPIO_NAME "reset_button"

struct simple_dev {
        dev_t devt;
        struct cdev cdev;
        struct semaphore sem;
        struct class *class;
        char *user_buff;
        long val;
};

static struct simple_dev reset_btn_dev;
#define USER_BUFF_SIZE 128

unsigned int irq_num = -1;
unsigned int irq_counter = 0;

static irqreturn_t reset_button_irq_handler(int irq, void *dev_id)
 {
    //printk(KERN_DEBUG "interrupt received (irq: %d)\n", irq);
    if (irq == gpio_to_irq(RESET_BTN_GPIO)) {
	irq_counter++;
	reset_btn_dev.val = irq_counter;
        printk(KERN_DEBUG "%s: you press button %d times, gpio pin is %d\n", RESET_BTN_GPIO_NAME, irq_counter, gpio_get_value(RESET_BTN_GPIO));
    }
    return IRQ_HANDLED;
}

static int reset_btn_cdev_open(struct inode *inode, struct file *filp)
{	
	struct simple_dev *sdev;
	int status = 0; 
	//printk("%s\n",__func__);    
	sdev = container_of(inode->i_cdev, struct simple_dev, cdev);
	//push to private_data
	filp->private_data = sdev;

	if (!sdev->user_buff) { 
		sdev->user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL); 	
	}
	if (!sdev->user_buff) { 
		printk("%s: user_buff alloc failed\n", __func__); 
		status = -ENOMEM; 
	} 
	//add semaphore for read/write
	up(&sdev->sem); 
	return status;
}

static int reset_btn_cdev_release(struct inode* inode , struct file* filp ) {
    struct simple_dev *sdev;
    sdev = container_of(inode->i_cdev, struct simple_dev, cdev);
    filp->private_data = NULL;
    if (down_interruptible(&sdev->sem)){
           return -ERESTARTSYS;
    }
    return 0;
}

static ssize_t reset_btn_cdev_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	struct simple_dev * sdev = filp->private_data;
        ssize_t status = 0;
        int len;
	//printk("%s\n",__func__);    
	if (*offp >= strlen(sdev->user_buff)){
		//printk("finish read %d bytes\n",(int)*offp);    
		return 0; 
	}
	if (down_interruptible(&sdev->sem)){ 
		return -ERESTARTSYS; 
	}
	len = sprintf(sdev->user_buff, "GPIO is %d ,the count is %ld\n", gpio_get_value(RESET_BTN_GPIO), sdev->val);
	// copy data to buff,    sdev->user_buff ---> buff
	if (copy_to_user(buff, sdev->user_buff, len)) { 
		status = -EFAULT; 
		return status; 
	}
	status = len; 
	*offp += len;
	up(&sdev->sem); 
	return status; 
}

static ssize_t reset_btn_cdev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	struct simple_dev * sdev = filp->private_data;
	ssize_t status = 0; 
	size_t len = USER_BUFF_SIZE - 1; 
	if (count == 0) {
		return 0;
	}
	//down semaphore 
	if (down_interruptible(&sdev->sem)) {	
		return -ERESTARTSYS; 
	} 
	if (len > count) {
		len = count; 
	}
	// echo X > /dev/dev_node, buff = X
	// copy_from_user(A,B,size) = A <---- B
	if (copy_from_user(sdev->user_buff, buff, len)) { 
		status = -EFAULT; 
	}
	//before convert str to int , need remove newline in the end
	sdev->user_buff[count] = '\0';
	//convert str to int like atoi
	status = kstrtol(sdev->user_buff,0,&sdev->val);
	// echo will send data and newline	 
	printk("%s: set sdev->val=%ld\n",__func__,sdev->val);
	//back the semaphore
	up(&sdev->sem);  
	status = count;
	return status; 
}

static const struct file_operations reset_btn_fops = {
        .owner = THIS_MODULE,
        .open = reset_btn_cdev_open,
        .release = reset_btn_cdev_release,
        .read = reset_btn_cdev_read,
        .write = reset_btn_cdev_write,
};



static ssize_t reset_btn_val_show(struct device* dev, struct device_attribute* attr, char* buf ) {
    struct simple_dev *sdev = (struct simple_dev*) dev_get_drvdata(dev);       
    long val = 0;       
    if(down_interruptible(&(sdev->sem))) {               
         return - ERESTARTSYS;       
    }       
    val = sdev->val;        
    up(&(sdev->sem));      
    return sprintf(buf, "GPIO is %d the count is %ld\n", gpio_get_value(RESET_BTN_GPIO),val);
return 0;
}

static ssize_t reset_btn_val_store(struct device* dev, struct device_attribute* attr, const char * buf, size_t count) {
    struct simple_dev *sdev = (struct simple_dev*) dev_get_drvdata(dev);       
    int val = 0;         
    val = simple_strtol(buf, NULL, 10 );
    if(down_interruptible(&(sdev->sem))) { 
         return - ERESTARTSYS;
    }
    sdev->val = val;
    up(&(sdev->sem));
    return count;
}



//asign dev_attr_reset_btn_val
static DEVICE_ATTR(reset_btn_val, S_IRUGO | S_IWUSR, reset_btn_val_show, reset_btn_val_store);


static ssize_t reset_btn_proc_read(char* page , char ** start, off_t off, int count, int * eof, void * data) {
    int val = 0;
    if(off > 0) {
         *eof = 1 ;
         return 0 ;
    }
    if(down_interruptible(&(reset_btn_dev.sem))) {
         return - ERESTARTSYS;
    }
    val = reset_btn_dev.val;
    up(&(reset_btn_dev.sem));
    return sprintf(page, "GPIO is %d the val is %d\n",gpio_get_value(RESET_BTN_GPIO), val);
}

static ssize_t reset_btn_proc_write(struct file* filp, const char __user *buff, unsigned long len, void* data ) {
    int status = 0; 
    char* page = NULL;
    int val = 0;
    if(len > PAGE_SIZE ) {
        printk (KERN_ALERT"The buff is too large: %lu.\n" , len);
         return - EFAULT;
    }
    page = (char*) __get_free_page(GFP_KERNEL);
    if(!page) {               
        printk("Failed to alloc page.\n" );
         return -ENOMEM;
    }      
    if(copy_from_user(page , buff, len)) {
        printk ("Failed to copy buff from user.\n" );               
        status = -EFAULT;
        goto out;
    }
    val = simple_strtol(page, NULL, 10);
    if(down_interruptible(&(reset_btn_dev.sem))) {
         return - ERESTARTSYS;
    }
    reset_btn_dev.val = val;
    up(&(reset_btn_dev.sem));
    status = len;
out:
    free_page((unsigned long )page);
    return status ;
}



static int reset_btn_init(void)
{
    int result;
    struct proc_dir_entry *reset_btn_entry;
    struct device *tmp = NULL;
    printk("%s\n", __func__);
    if( (result = gpio_is_valid(RESET_BTN_GPIO)) == 1){
	gpio_free(RESET_BTN_GPIO);
    }else{
	printk("%s: invalid gpio %d\n", RESET_BTN_GPIO_NAME, RESET_BTN_GPIO );
	return result;
    }
    if ((result = gpio_request(RESET_BTN_GPIO, RESET_BTN_GPIO_NAME)) != 0) 
    {
      printk("%s: GPIO request failure\n", RESET_BTN_GPIO_NAME );
      return -1;
    }
    if((result = gpio_direction_input(RESET_BTN_GPIO)) != 0){
	printk( "%s: cannot gpio %d direction\n", RESET_BTN_GPIO_NAME, RESET_BTN_GPIO );
	return -1;
    }

    if ((irq_num = gpio_to_irq(GPIO_TO_PIN(0, 17))) < 0 ) {
      printk("%s: GPIO to IRQ mapping failure \n", RESET_BTN_GPIO_NAME);
      return -1;
    }
	
    printk("%s: GPIO %d Mapped int %d\n",RESET_BTN_GPIO_NAME , RESET_BTN_GPIO , irq_num);
//IRQF_TRIGGER_HIGH
//IRQF_TRIGGER_LOW 
//IRQF_TRIGGER_RISING 
//IRQF_TRIGGER_FALLING 
    if ((result = request_irq(irq_num, (irq_handler_t)reset_button_irq_handler, IRQF_TRIGGER_RISING| IRQF_TRIGGER_FALLING , RESET_BTN_GPIO_NAME, NULL)) != 0) {
      printk("%s: Irq Request failure\n", RESET_BTN_GPIO_NAME);
      return -1;
   }
   printk("%s: registered IRQ %d\n", RESET_BTN_GPIO_NAME, irq_num );

// create char device
   //printk("memset \n");
	memset(&reset_btn_dev, 0, sizeof(reset_btn_dev)); 
   //printk("sema \n");
	sema_init(&reset_btn_dev.sem, 1); 
    // MKDEV will asign MAJOR and MINOR number
   //printk("devt \n");
    reset_btn_dev.devt = MKDEV(0, 0);
    //int alloc_chrdev_region(dev_t *dev,unsigned baseminor,unsigned count,const char *name); 
   //printk("alloc \n");
    alloc_chrdev_region(&reset_btn_dev.devt, 0, 1, "reset_btn");    
   //printk("cdev init \n");
    cdev_init(&reset_btn_dev.cdev, &reset_btn_fops);    
    // int cdev_add(struct cdev *dev, dev_t num, unsigned int count); 
    // usually count = 1
   //printk("cdev add \n");
    reset_btn_dev.cdev.owner = THIS_MODULE; 
    cdev_add(&reset_btn_dev.cdev, reset_btn_dev.devt, 1);  

    // create /sys/class/reset_btn_class folder
   //printk("class create \n");
    reset_btn_dev.class = class_create(THIS_MODULE, "reset_btn_class");    
    if(IS_ERR(reset_btn_dev.class)) {    
         printk("%s: failed in creating class./n", RESET_BTN_GPIO_NAME);    
         return -1;     
     } 
    // create /sys/devices/virtual/reset_btn_class/reset_key folder
    // ------uevent-------
    //| MAJOR=250         |
    //| MINOR=0           |
    //| DEVNAME=reset_key |
    // -------------------
    // ---dev---
    // | 250:0 |
    // ---------
    // subsystem ---> ../../../../class/reset_btn_class
    // in /sys/class/reset_btn_class folder, reset_key -> ../../devices/virtual/reset_btn_class/reset_key
   //printk("device create \n");
    tmp = device_create(reset_btn_dev.class, NULL, reset_btn_dev.devt, "%s", "reset_key"); 
    // create /sys/devices/virtual/reset_btn_class/reset_key/reset_btn_val
    result = device_create_file (tmp, &dev_attr_reset_btn_val);
    if(result < 0) {
        printk ("Failed to create attribute val." );               
         return -1;     
    }

    //save reset_btn_dev for reset_btn_val_show() and reset_btn_val_store()    
    dev_set_drvdata(tmp, &reset_btn_dev);
    //create proc
    reset_btn_entry = create_proc_entry("reset_btn", 0, NULL);
    if(reset_btn_entry) {
        reset_btn_entry->read_proc = reset_btn_proc_read;
        reset_btn_entry->write_proc = reset_btn_proc_write;
    }
    return 0;
}

static void reset_btn_exit(void)
{
	remove_proc_entry("reset_btn", NULL);
	device_destroy(reset_btn_dev.class, reset_btn_dev.devt); 
	class_destroy(reset_btn_dev.class); 

	cdev_del(&reset_btn_dev.cdev); 
	unregister_chrdev_region(reset_btn_dev.devt, 1); 

	//for resource
	//release_mem_region(mem_base, SZ_2K); 
	//iounmap(fpga_base); 

	//release_mem_region(mem_base, SZ_2K); 
	free_irq(gpio_to_irq(RESET_BTN_GPIO), NULL);
	gpio_free(RESET_BTN_GPIO);
	if (reset_btn_dev.user_buff) {
		kfree(reset_btn_dev.user_buff); 
	} 
   printk("%s: Remove module driver\n", RESET_BTN_GPIO_NAME);
}

module_init(reset_btn_init);
module_exit(reset_btn_exit);
