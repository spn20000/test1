#include <linux/module.h>     
#include <linux/init.h>      
#include <linux/i2c.h>      
#include <linux/gpio.h>  
#include "gt9271.h"


//对于有些芯片地址需要在上电时候才可以确定的情况，则不能使用i2c_new_probed_device
//因为 i2c_new_probed_device 会发送设备地址进行设备真实性探测，此时地址还没有确定，必然出错 
#define USE_PROBE                       0      //使用 i2c_new_probed_device    
#define SLAVE_ADDR                      0x5d   //从机地址



static struct i2c_client  *gt9271_clt;  //客户端的核心结构体，储存从机相关信息
static struct i2c_adapter *adap;     //i2c适配器，用于匹配驱动，传递数据

//平台数据    -- 固定对应管脚
struct gt9271_platform_data  gt9271_data={
	.gpio_irq    = 32+20,  //gpio0 占用32，<&gpio1 20 GPIO_ACTIVE_HIGH>; 
	.gpio_reset  = 32+13,  //gpio0 占用32，<&gpio1 13 GPIO_ACTIVE_LOW>;
};

//储存的是从设备信息
struct i2c_board_info  info ={
		.type  = "gt9271",  //设备名
		.flags = 0,      
		.addr  = SLAVE_ADDR,  //从机地址
		.platform_data = &gt9271_data, //设备里面用于传递自己私有内容的一个指针
};

//初始化函数
static int __init gt9271_dev_init(void)  
{	
	
	//1 表示gt9271 挂在I2C4总线上，可以看原理图知道
	adap = i2c_get_adapter(4) ;  //获得适配器地址，选择对应的控制器   
	if(adap==NULL){
		printk("error i2c_get_adapter\r\n");
		return -EINVAL;
	}

	gt9271_clt = i2c_new_device(adap,&info); //创建从机设备，通过返回值返回  
	//其实就相当于堆区创建一个客户端结构体,通过参数初始化里面的选项
	
	//gt9271_clt = i2c_new_probed_device(adap,&info, addr_list, NULL);
	if(gt9271_clt==NULL){
		printk("error i2c_new_device\r\n");
		return -ENOMEM;
	}

	printk(" create i2c new device success!\r\n");

	return 0;
}


//卸载函数
static void  __exit gt9271_dev_exit(void)  
{
	i2c_unregister_device(gt9271_clt);//注销客户端设备
	i2c_put_adapter(adap);//注销适配器
}

module_init(gt9271_dev_init);
module_exit(gt9271_dev_exit);

MODULE_LICENSE("Dual BSD/GPL");   
MODULE_AUTHOR("XYD");           
MODULE_VERSION("v1.0");
MODULE_DESCRIPTION("i2c device demo");  


#if 0
创建用户空间创建 i2c_client 的方法（这种方法不需要安装ts_client.ko）
创建并注册 i2c_client
echo gt9271 0x38 > /sys/bus/i2c/devices/i2c-1/new_device
注销 i2c_client
echo 0x38 > /sys/bus/i2c/devices/i2c-1/delete_device 
测试过程：
[root@zhifachen home]# echo gt9271 0x38 > /sys/bus/i2c/devices/i2c-1/new_device 
[  801.720000] gt9271_probe is call!
[  801.720000] name:gt9271,addr:56
[  801.725000] id->driver_data:123456
[  801.725000] i2c i2c-1: new_device: Instantiated device gt9271 at 0x38
[root@zhifachen home]# echo 0x38 > /sys/bus/i2c/devices/i2c-1/delete_device 
[  825.160000] i2c i2c-1: delete_device: Deleting device gt9271 at 0x38
[  825.165000] gt9271_remove is call!
[root@zhifachen home]# 

#endif

#if 0    //printk打印等级说明 

#define KERN_EMERG        "<0>" /* system is unusable */
#define KERN_ALERT        "<1>" /* action must be taken immediately */
#define KERN_CRIT         "<2>" /* critical conditions */
#define KERN_ERR          "<3>" /* error conditions */
#define KERN_WARNING  	  "<4>" /* warning conditions */
#define KERN_NOTICE       "<5>" /* normal but significant condition */
#define KERN_INFO         "<6>" /* informational */
#define KERN_DEBUG        "<7>" /* debug-level messages */
#endif


