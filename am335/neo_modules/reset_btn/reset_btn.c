#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h> // gpio_request(), gpio_to_irq()
#include <linux/interrupt.h> // request_irq(), irqreturn_t
#include <linux/cdev.h> // cdev, cdev_init(), cdev_add()
#include <linux/fs.h> //alloc_chrdev_region(), file_operations
#include <asm/uaccess.h> // copy_from_user()
#include <linux/slab.h> // kmalloc()
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
        printk("%s: you press button %d times, gpio pin is %d\n", RESET_BTN_GPIO_NAME, irq_counter, gpio_get_value(RESET_BTN_GPIO));
    }
    return IRQ_HANDLED;
}

static int reset_btn_cdev_open(struct inode *inode, struct file *filp)
{	
	int status = 0; 
	printk("%s\n",__func__);    
	if (!reset_btn_dev.user_buff) { 
		reset_btn_dev.user_buff = kmalloc(USER_BUFF_SIZE, GFP_KERNEL); 	
	}
	if (!reset_btn_dev.user_buff) { 
		printk("%s: user_buff alloc failed\n", __func__); 
		status = -ENOMEM; 
	} 
	//add semaphore for read/write
	up(&reset_btn_dev.sem); 
	//
	return status;
}
static ssize_t reset_btn_cdev_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	size_t len; 
	ssize_t status = 0; 
//	memset(reset_btn_dev.user_buff, 0, USER_BUFF_SIZE); 
	printk("%s\n",__func__);    

	//Generic user progs like cat will continue calling until we return zero. 
	//So if *offp != 0, we know this is at least the second call. 
	if (*offp > 0) {
		printk("offp > %d\n",(int)*offp);    
		return 0; 
	}

	//
	if (down_interruptible(&reset_btn_dev.sem)){ 
		return -ERESTARTSYS; 
	}
	strcpy(reset_btn_dev.user_buff, "fpga driver data goes here\n"); 
	len = strlen(reset_btn_dev.user_buff);
        printk("len=%d,count=%d, *offp=%d\n",len,count,(int)*offp);    
	if (copy_to_user(buff, reset_btn_dev.user_buff, len)) { 
		status = -EFAULT; 
		return status; 
	} 
	up(&reset_btn_dev.sem); 

	return status; 

/*
if (len > count) 
len = count; 
// int i,tmp; 	
	return buff_len;
*/
}

static ssize_t reset_btn_cdev_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	ssize_t status = count; 
	size_t len = USER_BUFF_SIZE - 1; 
	if (count == 0) {
		return 0;
	}

	//down semaphore 
	if (down_interruptible(&reset_btn_dev.sem)) {	
		return -ERESTARTSYS; 
	} 
	if (len > count) {
		len = count; 
	}

	memset(reset_btn_dev.user_buff, 0, USER_BUFF_SIZE); 

	if (copy_from_user(reset_btn_dev.user_buff, buff, len)) { 
		status = -EFAULT; 
	}
	// echo will send data and newline	 
	printk("%s: %s\n",__func__,reset_btn_dev.user_buff);
	//back the semaphore
	up(&reset_btn_dev.sem);  
	return status; 
}

static const struct file_operations reset_btn_fops = {
        .owner = THIS_MODULE,
        .open = reset_btn_cdev_open,
        .read = reset_btn_cdev_read,
        .write = reset_btn_cdev_write,
};

static int reset_btn_init(void)
{
    int result;
/*    dev_t reset_btn_devt;
    struct cdev reset_btn_cdev;
    static const struct file_operations reset_btn_fops = {
	.owner = THIS_MODULE,
	.open =	reset_btn_cdev_open,	
	.read =	reset_btn_cdev_read,
	.write = reset_btn_cdev_write,
    };
    struct class *reset_btn_class;*/
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
//IRQF_TRIGGER_FALLING 
    if ((result = request_irq(irq_num, (irq_handler_t)reset_button_irq_handler, IRQF_TRIGGER_HIGH| IRQF_TRIGGER_FALLING , RESET_BTN_GPIO_NAME, NULL)) != 0) {
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
    device_create(reset_btn_dev.class, NULL, reset_btn_dev.devt, NULL, "reset_key");     
    return 0;
}

static void reset_btn_exit(void)
{
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
