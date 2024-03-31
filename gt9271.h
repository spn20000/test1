/*
 * Goodix GT9xx touchscreen driver
 */
#ifndef _GOODIX_GT9XX_H_
#define _GOODIX_GT9XX_H_

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
/*******************************************比较重要的信息*********************************************************/
//平台数据   --  就是保存中断管脚和复位管脚
struct gt9271_platform_data {
    uint32_t gpio_irq;          // IRQ port
    uint32_t gpio_reset;        // Reset support
};

//驱动数据结构    ---   以前封装的关于按键的结构类似，主要存放希望用到的数据
struct goodix_ts_data {
    spinlock_t irq_lock;  //中断的旋转锁，保护资源的完整性
    struct i2c_client *client; //保存客户端的所有信息
    struct input_dev  *input_dev; //输入子系统会使用的输入设备，触摸屏触摸中断上报数据处理
    struct work_struct  work;  //使用工作队列
    s32 irq_is_disable;  //标志位
    u16 abs_x_max; //绝对路径x轴最大范围，上报绝对事件需要用到最大范围
    u16 abs_y_max; //绝对路径y轴最大范围，上报绝对事件需要用到最大范围
    u8  max_touch_num; //最大运行触摸点 -- 属于多点触发
    u8  int_trigger_type; 
    int gtp_cfg_len;  //记录长度信息的标志位
    u8  pnl_init_error; 
};
//触摸屏里面使用工作队列、输入子系统上报、i2c子系统


//byte0:配置版本     根据内核提供数据配置的
#define CTP_CFG_GROUP5 {\
        /* HD702 :800×1280(w*h) :0x0320x0x0500   */ \
        0x50,0x20,0x03,0x00,0x05,0x05,0x34,0x20,0x02,0x2B,0x28,\
        0x0F,0x50,0x3C,0x03,0x05,0x00,0x00,0x00,0x00,0x00,0x00,\
        0x00,0x18,0x1A,0x1E,0x14,0x8D,0x2D,0x88,0x3A,0x37,0x33,\
        0x0F,0x00,0x00,0x00,0x02,0x02,0x2D,0x00,0x00,0x00,0x00,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1C,0x41,0x94,0xC5,\
        0x02,0x07,0x00,0x00,0x04,0xD6,0x1E,0x1E,0xB6,0x24,0x00,\
        0x9F,0x2A,0x00,0x8A,0x32,0x00,0x79,0x3B,0x00,0x79,0x00,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
        0x00,0x00,0x00,0x01,0x04,0x05,0x06,0x07,0x08,0x09,0x0C,\
        0x0D,0x0E,0x0F,0x10,0x11,0x14,0x15,0xFF,0xFF,0xFF,0xFF,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x27,\
        0x26,0x25,0x24,0x23,0x22,0x21,0x20,0x1F,0x1E,0x1C,0x1B,\
        0x19,0x13,0x12,0x11,0x10,0x0F,0x0C,0x0A,0x08,0x07,0x06,\
        0x04,0x02,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,\
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x01\
    }
	
	

// STEP_3(optional): Specify your special config info if needed
//设置触摸点的最大点数
#define GTP_MAX_TOUCH    5

//#define GTP_ICS_SLOT_REPORT   0   //非0表示使用 B协议 slot protocol 



#define GTP_I2C_NAME          "gt9271"  
//看时序图的时候需要写两个数据到从机，就是这个数据大小 
#define GTP_ADDR_LENGTH       2
//这个是发送数据包的最小和最大范围
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240



// Registers define
//相关的从器件寄存器地址    用于传输的时候确认读取数据或者写入数据的地点
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_SLEEP         0x8040  //传感器休眠寄存器
#define GTP_REG_SENSOR_ID     0x814A  //传感器ID号寄存器
#define GTP_REG_CONFIG_DATA   0x8047  //配置数据寄存器
#define GTP_REG_VERSION       0x8140  //版本号寄存器



/*******************************************比较次要的信息*********************************************************/


//***************************PART3:OTHER define*********************************
#define GTP_DRIVER_VERSION    "V2.4 <2014/11/28>"



#define FAIL                  0
#define SUCCESS               1



#define RESOLUTION_LOC        3       //配置数组中屏分辨率参数的数值
#define TRIGGER_LOC           8       //配置数组中触发方式参数的数值


//通过指针判断数组的元素个数
#define CFG_GROUP_LEN(p_cfg_grp)  (sizeof(p_cfg_grp) / sizeof(p_cfg_grp[0]))




// Log define
#define GTP_DEBUG_ON          1   //打印普通debug信息开/关
#define GTP_DEBUG_ARRAY_ON    0   //打印数组开/关
#define GTP_DEBUG_FUNC_ON     0   //打印函数开/关



// STEP_2(REQUIRED): Customize your I/O ports & I/O operations
#define GTP_GPIO_AS_INPUT(pin)          do{\
        gpio_direction_input(pin);\
    }while(0)
//上面等价于 #define 	GTP_GPIO_AS_INPUT(pin)  gpio_direction_input(pin)   其实就是设置管脚为输入
	
//再次封装    其实就是初始化int的中断管脚，设置为输入
#define GTP_GPIO_AS_INT(pin)            do{\
        GTP_GPIO_AS_INPUT(pin);\
    }while(0)

//获取对应管脚的电平
#define GTP_GPIO_GET_VALUE(pin)         gpio_get_value(pin)

//设置输出和输出电平
#define GTP_GPIO_OUTPUT(pin,level)      gpio_direction_output(pin,level)
#define GTP_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define GTP_GPIO_FREE(pin)              gpio_free(pin)
#define GTP_IRQ_TAB                     {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_LOW, IRQ_TYPE_LEVEL_HIGH}


//下面都是打印信息封装
#define GTP_INFO(fmt,arg...)           printk(KERN_WARNING"<<-GTP-INFO->> "fmt"\n",##arg)
#define GTP_ERROR(fmt,arg...)          printk(KERN_WARNING"<<-GTP-ERROR->> "fmt"\n",##arg)
#define GTP_DEBUG(fmt,arg...)          do{\
        if(GTP_DEBUG_ON)\
            printk(KERN_WARNING"<<-GTP-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
    }while(0)

//打印对应数组里面num个数的内容
#define GTP_DEBUG_ARRAY(array, num)    do{\
        s32 i;\
        u8* a = array;\
        if(GTP_DEBUG_ARRAY_ON)\
        {\
            printk(KERN_WARNING"<<-GTP-DEBUG-ARRAY->>\n");\
            for (i = 0; i < (num); i++)\
            {\
                printk("%02x   ", (a)[i]);\
                if ((i + 1 ) %10 == 0)\
                {\
                    printk("\n");\
                }\
            }\
            printk("\n");\
        }\
    }while(0)


#define GTP_DEBUG_FUNC()               do{\
        if(GTP_DEBUG_FUNC_ON)\
            printk(KERN_WARNING"<<-GTP-FUNC->> Func:%s@Line:%d\n",__func__,__LINE__);\
    }while(0)

#endif /* _GOODIX_GT9XX_H_ */



