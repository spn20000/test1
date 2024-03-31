/*
 * Goodix GT9xx touchscreen driver
 */
#include <linux/irq.h>
#include <linux/platform_data/ctouch.h>
#include <linux/platform_data/goodix_touch.h>
#include <linux/input/mt.h>
#include "gt9271.h"

static const char *goodix_ts_name    = "goodix-ts";
static const char *goodix_input_phys = "input/ts";
static struct workqueue_struct *goodix_wq;
static int gtp_rst_gpio = -1;  //复位引用编号
static int gtp_int_gpio = -1;  //中断引脚编号

static u8 config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH] = {
    GTP_REG_CONFIG_DATA >> 8,GTP_REG_CONFIG_DATA & 0xff
};

static s8 gtp_i2c_test(struct i2c_client *client);
static void gtp_reset_guitar(struct i2c_client *client, s32 ms);
static s32 gtp_send_cfg(struct i2c_client *client);
static void gtp_int_sync(s32 ms);


/*******************************************************
Function:
Read data from the i2c slave device.
Input:
client:     i2c device.
buf[0~1]:   read start address.
buf[2~len-1]:   read data buffer.
len:    GTP_ADDR_LENGTH + read bytes count
Output:
numbers of i2c_msgs to transfer:
2: succeed, otherwise: failed
 *********************************************************/
static s32 gtp_i2c_read(struct i2c_client *client, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret = -1;
    s32 retries = 0;

    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = GTP_ADDR_LENGTH;
    msgs[0].buf   = &buf[0];

    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len - GTP_ADDR_LENGTH;
    msgs[1].buf   = &buf[GTP_ADDR_LENGTH];

    while (retries < 3) {
        ret = i2c_transfer(client->adapter, msgs, 2);
        if (ret == 2)
            break;
        retries++;
    }

    //大于等于3表示前面读取数据出错了
    if ((retries >= 3)) {
        GTP_ERROR("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.",
                  (((u16)(buf[0] << 8)) | buf[1]), len - 2, ret);

        gtp_reset_guitar(client, 10);//读取数据出错重新复位芯片;
    }

    return ret;
}

/*******************************************************
Function:
Write data to the i2c slave device.
Input:
client:     i2c device.
buf[0~1]:   write start address.
buf[2~len-1]:   data buffer
len:    GTP_ADDR_LENGTH + write bytes count
Output:
numbers of i2c_msgs to transfer:
1: succeed, otherwise: failed
 *********************************************************/
static  s32 gtp_i2c_write(struct i2c_client *client, u8 *buf, s32 len)
{
    struct i2c_msg msg;
    s32 ret = -1;
    s32 retries = 0;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len;
    msg.buf   = buf;

    while(retries < 5) {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)
            break;
        retries++;
    }

    //连续失败5次,则重新复位芯片
    if ((retries >= 5)) {
        GTP_ERROR("I2C Write: 0x%04X, %d bytes failed, errcode: %d! Process reset.",
                  (((u16)(buf[0] << 8)) | buf[1]), len - 2, ret);
        gtp_reset_guitar(client, 10);
    }

    return ret;
}


/*******************************************************
Function:
Send config.
Input:
client: i2c device.
Output:
result of i2c write operation.
1: succeed, otherwise: failed
 *********************************************************/
static  s32 gtp_send_cfg(struct i2c_client *client)
{
    s32 ret = 2;
    s32 retry = 0;
    struct goodix_ts_data *ts = i2c_get_clientdata(client);

    if (ts->pnl_init_error) {
        GTP_INFO("Error occured in init_panel, no config sent");
        return 0;
    }

    GTP_INFO("Driver send config.");
    //尝试最多5次发送配置文件给触摸屏控制芯片
    for (retry = 0; retry < 5; retry++) {
        ret = gtp_i2c_write(client, config, GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
        if (ret > 0) {
            break;
        }
    }
    return ret;
}

/*******************************************************
Function:
Disable irq function
Input:
ts: goodix i2c_client private data
Output:
None.
 *********************************************************/
static void gtp_irq_disable(struct goodix_ts_data *ts)
{
    unsigned long irqflags;

    GTP_DEBUG_FUNC();
    spin_lock_irqsave(&ts->irq_lock, irqflags);
    if (!ts->irq_is_disable) {
        //设置中断禁止标志
        ts->irq_is_disable = 1;
        //此处不能使用 disable_irq,因为中断中使用这个函数会内核死锁
        disable_irq_nosync(ts->client->irq);
    }
    spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

/*******************************************************
Function:
Enable irq function
Input:
ts: goodix i2c_client private data
Output:
None.
 *********************************************************/
static void gtp_irq_enable(struct goodix_ts_data *ts)
{
    unsigned long irqflags = 0;
    spin_lock_irqsave(&ts->irq_lock, irqflags);
    if (ts->irq_is_disable) {
        enable_irq(ts->client->irq);
        ts->irq_is_disable = 0;
    }
    spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

/*******************************************************
Function:
Report touch point event
Input:
ts: goodix i2c_client private data
id: trackId
x:  input x coordinate
y:  input y coordinate
w:  input pressure
Output:
None.
 *********************************************************/
static void gtp_touch_down(struct goodix_ts_data *ts, s32 id, s32 x, s32 y, s32 w)
{
    input_report_key(ts->input_dev, BTN_TOUCH, 1);
    input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
    input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
    input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
    input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
    input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
    input_mt_sync(ts->input_dev);
    GTP_DEBUG("ID:%d, X:%d, Y:%d, W:%d", id, x, y, w);
}

/*******************************************************
Function:
Report touch release event
Input:
ts: goodix i2c_client private data
Output:
None.
 *********************************************************/
static void gtp_touch_up(struct goodix_ts_data *ts, s32 id)
{
    input_report_key(ts->input_dev, BTN_TOUCH, 0);
    input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
    input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
    input_mt_sync(ts->input_dev);
}

/*******************************************************
Function:
Goodix touchscreen work function
Input:
work: work struct of goodix_workqueue
Output:
None.
 *********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
    u8  end_cmd[3] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF, 0};

    //查询数据手册可知道一个坐标信息使用8字节空间存储，前面两字节是坐标寄存器起始地址
    u8  point_data[2 + 1 + 8 * GTP_MAX_TOUCH + 1] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF};
    u8  touch_num = 0;
    u8  finger = 0;
    u8 *coor_data = NULL;
    s32 input_x = 0;
    s32 input_y = 0;
    s32 input_w = 0;
    s32 id = 0;
    s32 i  = 0;
    s32 ret = -1;
    struct goodix_ts_data *ts = NULL;
    //注意，这里设置为静态变量，这个值是记录上一次已经按下的触点
    static u16 pre_touch = 0;

    ts = container_of(work, struct goodix_ts_data, work);

    //读取12字节数据，
    //后面再根据这些信息判断是否需要进行读取坐标
    ret = gtp_i2c_read(ts->client, point_data, 12);
    if (ret < 0) {
        GTP_ERROR("I2C transfer error. errno:%d\n ", ret);
        gtp_irq_enable(ts);
        return;
    }

    //取得触摸屏状态信息
    finger = point_data[GTP_ADDR_LENGTH];
    //没有触摸点按下
    if (finger == 0x00)  {
        gtp_irq_enable(ts);//重新使能中断
        return;

    }

    //位7：缓冲区状态，1 =坐标（或键）准备好主机读取; 0 =坐标（或键）未就绪且数据无效。
    //读取坐标后，主机应通过I2C将此标志（或整个字节）设置为0
    if((finger & 0x80) == 0) {
        goto exit_work_func;
    }

    //取触摸屏触点数量
    touch_num = finger & 0x0f;
    if (touch_num > GTP_MAX_TOUCH) {
        goto exit_work_func;
    }

    //触点数量大于1，读取所有触点数据
    if (touch_num > 1) {
        //GTP_READ_COOR_ADDR + 10 等于 0x8158，刚刚好的第一个x坐标寄存器地址
        u8 buf[8 * GTP_MAX_TOUCH] = {(GTP_READ_COOR_ADDR + 10) >> 8, (GTP_READ_COOR_ADDR + 10) & 0xff};
        //读取当前所有的触点数据
        ret = gtp_i2c_read(ts->client, buf, 2 + 8 * (touch_num - 1));

        //前面已经有12字节数据在 point_data中了，跳过前面12字节保存坐标数据
        memcpy(&point_data[12], &buf[2], 8 * (touch_num - 1));
    }

    //GTP_DEBUG("pre_touch:%02x, finger:%02x.", pre_touch, finger);

    //处理坐标数据
    if (touch_num)  {
        for (i = 0; i < touch_num; i++) {
            coor_data = &point_data[i * 8 + 3];
            id = coor_data[0] & 0x0F;                       //坐标id
            input_x  = coor_data[1] | (coor_data[2] << 8);
            input_y  = coor_data[3] | (coor_data[4] << 8);
            input_w  = coor_data[5] | (coor_data[6] << 8);
            gtp_touch_down(ts, id, input_x, input_y, input_w);
        }
    } else {
        GTP_DEBUG("Touch Release!");
        gtp_touch_up(ts, 0);
    }

    input_sync(ts->input_dev);  //同步事件

exit_work_func:
    ret = gtp_i2c_write(ts->client, end_cmd, 3);
    if (ret < 0) {
        GTP_INFO("I2C write end_cmd error!");
    }
    //重新使能中断
    gtp_irq_enable(ts);
}


/*******************************************************
Function:
External interrupt service routine for interrupt mode.
Input:
irq:  interrupt number.
dev_id: private data pointer
Output:
Handle Result.
IRQ_HANDLED: interrupt handled successfully
 *********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
    struct goodix_ts_data *ts = dev_id;
    gtp_irq_disable(ts);
    queue_work(goodix_wq, &ts->work);
    return IRQ_HANDLED;
}

/*******************************************************
Function:
Synchronization.
Input:
ms: synchronization time in millisecond.
Output:
None.
 *******************************************************/
void gtp_int_sync(s32 ms)
{
    GTP_GPIO_OUTPUT(gtp_int_gpio, 0);
    msleep(ms);
    GTP_GPIO_AS_INT(gtp_int_gpio);
}

/*******************************************************
Function:
Reset chip.
Input:
ms: reset time in millisecond
Output:
None.
 *******************************************************/
void gtp_reset_guitar(struct i2c_client *client, s32 ms)
{
    if (gpio_is_valid(gtp_rst_gpio)) {
        GTP_GPIO_OUTPUT(gtp_rst_gpio, 0);   // begin select I2C slave addr
        msleep(ms);                         // T2: > 10ms
    }

    // HIGH: 0x28/0x29, LOW: 0xBA/0xBB
    GTP_GPIO_OUTPUT(gtp_int_gpio, client->addr == 0x14);
    msleep(2);                          // T3: > 100us

    if (gpio_is_valid(gtp_rst_gpio)) {
        GTP_GPIO_OUTPUT(gtp_rst_gpio, 1);
        msleep(6);                          // T4: > 5ms

        GTP_GPIO_AS_INPUT(gtp_rst_gpio);    // end select I2C slave addr
    }

    gtp_int_sync(50);                       //根据芯片手册其中只需要5ms

}


/*******************************************************
Function:
Initialize gtp.
Input:
ts: goodix private data
Output:
Executive outcomes.
0: succeed, otherwise: failed
 *******************************************************/
static s32 gtp_init_panel(struct goodix_ts_data *ts)
{
    s32 ret = -1;
    s32 i = 0;
    u8 check_sum = 0;
    u8 cfg_info_group5[] = CTP_CFG_GROUP5;
	
    ts->gtp_cfg_len = CFG_GROUP_LEN(cfg_info_group5);
    memset(&config[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
    memcpy(&config[GTP_ADDR_LENGTH], cfg_info_group5, ts->gtp_cfg_len);

    config[TRIGGER_LOC] |= 0x01;  //设置触发方式
    check_sum = 0;
    for (i = GTP_ADDR_LENGTH; i < ts->gtp_cfg_len; i++) {
        check_sum += config[i];
    }
    config[ts->gtp_cfg_len] = (~check_sum) + 1;

    if ((ts->abs_x_max == 0) && (ts->abs_y_max == 0)) {
        ts->abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
        ts->abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
        ts->int_trigger_type = (config[TRIGGER_LOC]) & 0x03;
    }

    ret = gtp_send_cfg(ts->client);
    if (ret < 0) {
        GTP_ERROR("Send config error.");
    }

    GTP_INFO("X_MAX: %d, Y_MAX: %d, TRIGGER: 0x%02x", ts->abs_x_max, ts->abs_y_max,
             ts->int_trigger_type);

    msleep(10);
    return 0;
}

/*******************************************************
Function:
Read chip version.
Input:
client:  i2c device
version: buffer to keep ic firmware version
Output:
read operation return.
2: succeed, otherwise: failed
 *******************************************************/
s32 gtp_read_version(struct i2c_client *client, u16 *version)
{
    s32 ret = -1;
    u8 buf[8] = {GTP_REG_VERSION >> 8, GTP_REG_VERSION & 0xff};

    GTP_DEBUG_FUNC();
    ret = gtp_i2c_read(client, buf, sizeof(buf));
    if (ret < 0) {
        GTP_ERROR("GTP read version failed");
        return ret;
    }

    if (version) {
        *version = (buf[7] << 8) | buf[6];
    }

    if (buf[5] == 0x00) {
        GTP_INFO("IC Version: %c%c%c_%02x%02x", buf[2], buf[3], buf[4], buf[7], buf[6]);
    } else {
        GTP_INFO("IC Version: %c%c%c%c_%02x%02x", buf[2], buf[3], buf[4], buf[5], buf[7], buf[6]);
    }
    return ret;
}

/*******************************************************
Function:
I2c test Function.
Input:
client:i2c client.
Output:
Executive outcomes.
2: succeed, otherwise failed.
 *******************************************************/
static s8 gtp_i2c_test(struct i2c_client *client)
{
    u8 test[3] = {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};
    u8 retry = 0;
    s8 ret = -1;

    while (retry++ < 3) {
        ret = gtp_i2c_read(client, test, 3);
        if (ret > 0) {
            return ret;
        }
        GTP_ERROR("GTP i2c test failed time %d.", retry);
        msleep(10);
    }
    return ret;
}

/*******************************************************
Function:
Request gpio(INT & RST) ports.
Input:
ts: private data.
Output:
None.
 *******************************************************/
static void gtp_free_io_port(struct goodix_ts_data *ts)
{
    GTP_GPIO_FREE(gtp_int_gpio);
    if (gpio_is_valid(gtp_rst_gpio))
        GTP_GPIO_FREE(gtp_rst_gpio);
}

/*******************************************************
Function:
Request gpio(INT & RST) ports.
Input:
ts: private data.
Output:
Executive outcomes.
>= 0: succeed, < 0: failed
 *******************************************************/
static s8 gtp_request_io_port(struct goodix_ts_data *ts)
{
    s32 ret = 0;

    //申请中断编号
    ret = GTP_GPIO_REQUEST(gtp_int_gpio, "GTP INT IRQ");
    if (ret < 0) {
        GTP_ERROR("Failed to request GPIO:%d, ERRNO:%d", (s32)gtp_int_gpio, ret);
        return -ENODEV;
    } else {
        GTP_GPIO_AS_INT(gtp_int_gpio);                //配置为输入
        ts->client->irq = gpio_to_irq(gtp_int_gpio);  //gpio转换为中断编号
    }

    //检测复位引脚编号是否有效
    if (gpio_is_valid(gtp_rst_gpio)) {
        //申请复位引脚编号
        ret = GTP_GPIO_REQUEST(gtp_rst_gpio, "GTP RST PORT");
        if (ret < 0) {
            GTP_ERROR("Failed to request GPIO:%d, ERRNO:%d", (s32)gtp_rst_gpio, ret);
            GTP_GPIO_FREE(gtp_int_gpio);
            ret = -ENODEV;
        }
        GTP_GPIO_AS_INPUT(gtp_rst_gpio);
    }
    //设置为芯片地址为0xBA/0xBB （纯地址就是0x5D)
    gtp_reset_guitar(ts->client, 20);

    return ret;
}

/*******************************************************
Function:
Request interrupt.
Input:
ts: private data.
Output:
Executive outcomes.
0: succeed, -1: failed.
 *******************************************************/
static s8 gtp_request_irq(struct goodix_ts_data *ts)
{
    s32 ret = -1;
    const u8 irq_table[] = GTP_IRQ_TAB;
    GTP_DEBUG("INT trigger type:%x", ts->int_trigger_type);

    ret  = request_irq(ts->client->irq,
                       goodix_ts_irq_handler,
                       irq_table[ts->int_trigger_type],
                       ts->client->name,
                       ts);
    if (ret) {
        GTP_ERROR("Request IRQ failed!ERRNO:%d.", ret);
        return -1;
    } else {
        gtp_irq_disable(ts);
        return 0;
    }
}

/*******************************************************
Function:
Request input device Function.
Input:
ts:private data.
Output:
Executive outcomes.
0: succeed, otherwise: failed.
 *******************************************************/
static s8 gtp_request_input_dev(struct goodix_ts_data *ts)
{
    s8 ret = -1;

    ts->input_dev = input_allocate_device();   //分配 input_dev结构
    if (ts->input_dev == NULL) {
        GTP_ERROR("Failed to allocate input device.");
        return -ENOMEM;
    }
	
    input_set_capability(ts->input_dev, EV_KEY, BTN_TOUCH);
    input_set_capability(ts->input_dev, EV_ABS, ABS_MT_POSITION_X);
    input_set_capability(ts->input_dev, EV_ABS, ABS_MT_POSITION_Y);
    input_set_capability(ts->input_dev, EV_ABS, ABS_MT_WIDTH_MAJOR);
    input_set_capability(ts->input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR);
    input_set_capability(ts->input_dev, EV_ABS, ABS_MT_TRACKING_ID);

    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

    ts->input_dev->name = goodix_ts_name;
    ts->input_dev->phys = goodix_input_phys;
    ts->input_dev->id.bustype = BUS_I2C;
    ts->input_dev->id.vendor = 0xDEAD;
    ts->input_dev->id.product = 0xBEEF;
    ts->input_dev->id.version = 10427;

    ret = input_register_device(ts->input_dev);
    if (ret) {
        GTP_ERROR("Register %s input device failed", ts->input_dev->name);
        return -ENODEV;
    }
    return 0;
}


/**
 * gtp_parse_dt - parse platform infomation form devices tree.
 */
static void gtp_parse_dt(struct device *dev)
{
    struct device_node *np = dev->of_node;
    gtp_int_gpio = of_get_named_gpio(np, "goodix,irq-gpio", 0);
    gtp_rst_gpio = of_get_named_gpio(np, "goodix,rst-gpio", 0);
}

/*******************************************************
Function:
I2c probe.
Input:
client: i2c device struct.
id: device id.
Output:
Executive outcomes.
0: succeed.
 *******************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

    int ret = -1;
    u16 version_info;
    struct goodix_ts_data *ts;//创建储存相关信息的结构体
    struct gt9271_platform_data *pdata;//里面就是中断和复位的io管脚号


    //检测适配器是否支持I2C标准协议时序    --- 简单测试
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        GTP_ERROR("I2C check functionality failed.");
        return -ENODEV;
    }


    //创建工作队列    --- 创建工作队列，不使用共享队列，需要响应及时
    goodix_wq = create_singlethread_workqueue("goodix_wq");
    if (!goodix_wq) {
        GTP_ERROR("Creat workqueue failed.");
        return -ENOMEM;
    }


    //分配核心结构体     创建一个相关的结构体空间，给里面的变量储存的位置
    ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    if (!ts) {
        GTP_ERROR("Alloc GFP_KERNEL memory failed.");
        ret = -ENOMEM;
        goto error_kzalloc;
    }

    //初始化工作队列       初始化工作任务
    INIT_WORK(&ts->work, goodix_ts_work_func);
	
	//对结构体赋值
    ts->client = client;
	
	//初始化锁
    spin_lock_init(&ts->irq_lock);         // 初始化自旋锁，用于保护中断标志变量

	//不使用这个函数也可以，直接往dev->platform_data就可以了
    pdata = dev_get_platdata(&client->dev); //从平台数据中解析数据
    if (!pdata) {
        dev_warn(&client->dev, "no platform data supplied\n");
        ret = -EINVAL;
        goto error_no_pdata;
    }
	
	//初始优化这两个相关管脚，中断和复位
    gtp_int_gpio = pdata->gpio_irq;     //保存中断引脚编号
    gtp_rst_gpio = pdata->gpio_reset;   //保存复位引用编号


	//把前面的数据结构通过platform_data这个参数进行传递
    i2c_set_clientdata(client, ts);         //保存 ts 驱动数据结构数据到 i2c_client 结构中;

    
	//申请占用这两个管脚
    ret = gtp_request_io_port(ts);          //申请中断引脚，及复位引脚;
    if (ret < 0) {
        GTP_ERROR("GTP request IO port failed.");
        ret = -EBUSY;
        goto error_gtp_request_io_port;
    }
	
	//测试看一下从机有没有回应
    ret = gtp_i2c_test(client);                    //读取数据测试芯片是否正常工作;
    if (ret < 0) {
        GTP_ERROR("I2C communication ERROR!");
        ret = -ENODEV;
        goto error_gtp_i2c_test;
    }
	
	//测试使用，可不读取
    ret = gtp_read_version(client, &version_info); //读取芯片版本;
    if (ret < 0) {
        GTP_ERROR("Read version failed.");
        ret = -ENODEV;
        goto error_gtp_read_version;
    }

	//上面封装的一个函数
    ret = gtp_init_panel(ts);                      //初始化触摸屏
    if (ret < 0) {
        GTP_ERROR("GTP init panel failed.");
        ret = -EINVAL;
        goto error_gtp_init_panel;
    }
	
	//初始化输入子系统，也是上面封装的函数
    ret = gtp_request_input_dev(ts);               //注册Input设备;
    if (ret < 0) {
        GTP_ERROR("GTP request input dev failed");
        goto error_gtp_request_input_dev;
    }
	
	//注册中断
    ret = gtp_request_irq(ts);                     //注册中断;
    if (ret < 0) {
        ret = -EINVAL;
        goto  error_gtp_request_irq;
    }

	//也是封装的函数，
    gtp_irq_enable(ts);                            //使能中断;
	
    return 0;
error_gtp_request_irq:
    input_unregister_device(ts->input_dev);
error_gtp_request_input_dev:
error_gtp_init_panel:
error_gtp_read_version:
error_gtp_i2c_test:
    gtp_free_io_port(ts);
error_gtp_request_io_port:
    i2c_set_clientdata(client, NULL);
error_no_pdata:
    kfree(ts);
error_kzalloc:
    destroy_workqueue(goodix_wq);
    return ret;
}


/*******************************************************
Function:
Goodix touchscreen driver release function.
Input:
client: i2c device struct.
Output:
Executive outcomes. 0---succeed.
 *******************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	
    struct goodix_ts_data *ts;
    ts = i2c_get_clientdata(client);               //取得驱动数据结构地址
    GTP_DEBUG_FUNC();                              //输出调试信息
    free_irq(client->irq, ts);                     //注销
    gtp_free_io_port(ts);                          //释放中断引脚和复位引脚
    i2c_set_clientdata(client, NULL);              //释放i2c_client中的驱动数据
    input_unregister_device(ts->input_dev);        //注销input设备
    kfree(ts);                                     //释放驱动数据结构
    destroy_workqueue(goodix_wq);                  //销毁工具队列
    GTP_INFO("GTP driver removing  success!\r\n");
    return 0;
}


//设备树实现实现层的匹配的设备id
static const struct of_device_id goodix_match_table[] = {
    {.compatible = "goodix,gt9xx",},
    { },
};

//非设备树实现实现层的匹配的设备id
static const struct i2c_device_id goodix_ts_id[] = {
    { GTP_I2C_NAME, 0 },
    { }
};

static struct i2c_driver goodix_ts_driver = {
    .probe      = goodix_ts_probe,//匹配运行的函数
    .remove     = goodix_ts_remove,//取消匹配运行的函数
    .id_table   = goodix_ts_id,//匹配自己写的客户端设备
    .driver = {
        .name     = GTP_I2C_NAME,//通过那么匹配设备，其他两种方式实现的时候，这种方式不会使用
        .owner    = THIS_MODULE,//拥有者
        .of_match_table = of_match_ptr(goodix_match_table),//匹配设备树的节点of_match_ptr这个只是判断指针是否为NULL
    },
};


/*******************************************************
Function:
Driver Install function.
Input:
None.
Output:
Executive Outcomes. 0---succeed.
 ********************************************************/
static int __init goodix_ts_init(void)
{
    s32 ret = 0;

    ret = i2c_add_driver(&goodix_ts_driver);
    if(ret < 0) {
        return ret;
    }

    return ret;
}


/*******************************************************
Function:
Driver uninstall function.
Input:
None.
Output:
Executive Outcomes. 0---succeed.
 ********************************************************/
static void __exit goodix_ts_exit(void)
{

    i2c_del_driver(&goodix_ts_driver);
}

module_init(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("GTP Series Driver");
MODULE_LICENSE("GPL");

