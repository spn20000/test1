#include "ts.h"

int temp[3]={0};//用于保存滑动时读取的xy坐标  0 是x坐标  1 是y坐标
int timer=0;//判断是否是点击   超过时间代表滑动
int temp1[3]={0};//用于保存点击时读取的xy坐标  0 是x坐标  1 是y坐标

int touch=0;//1代表上一张   2下一张    需要外部引用声明

/*
   函数功能：读取触摸屏滑动值
   函数形参：
   int fd 被打开的文件
   函数返回值：
 */
void hua_read(int fd,int id)
{
	struct input_event buf;//读取到的数据结构  输入子系统的标准
	int flag=0;
	while(1)
	{
		//读取文件里面的内容
		read(fd,&buf,sizeof(buf));

		//分析数据
		switch(buf.type)//分析是什么事件
		{
			case EV_KEY:
				{
					if(buf.value)//最先受到的数据是屏幕按下
					{
						printf("触摸屏按下\r\n");
					}else//按键松开之后，滑动结束
					{
						printf("触摸屏松开\r\n");
						return ;
					}
					break;
				}
			case EV_ABS:
				{

					switch(buf.code)
					{
						case ABS_MT_POSITION_X:
							{
								if(temp[0] != buf.value)//判断数值和上一次读到的x州坐标有没有变化
								{
									temp[0] = buf.value;//从新写入新值
							//		flag=1;//给最后获取的内容作标记  代表屏幕x位置发生改变
								}
								break;
							}
						case ABS_MT_POSITION_Y:
							{
								if(temp[1] != buf.value)//判段y数据是否改变
								{
									temp[1] = buf.value;//从新写入
								//	flag=1;//数值改变标记
								}
								break;
							}
						case ABS_MT_TRACKING_ID:
							{
								/*
								if(buf.value == id)//判断是否是对应的触点
								{
									if(flag)//有改动就进来输出信息
									{
										flag = 0;//从新置零
										
										//显示更新之后的数值  不想显示就直接屏蔽就行
										printf("x=%d\r\n",temp[0]);
										printf("y=%d\r\n",temp[1]);
										printf("id=%d\r\n",id);
										
									}
								}
								*/
								break;
							}
					}
				}

				break;

			case EV_SYN:
				{
					break;
				}
		}
	}

}


//线程处理函数
void *count(void *arg)
{
	while(1)//只做计时
	{
		usleep(10000);
		timer++;
	}
}



/*
   函数功能：读取触摸屏返回的数值
   函数形参：
   char *tspath
   函数返回值：None
 */
void ts_read(char *tspath)
{
	//打开文件
	int fd = open(tspath,O_RDWR);

	//循环读取文件
	struct input_event buf;
	int flag=1;

	//创建线程计时
	pthread_t tid=0;
	pthread_create(&tid,NULL,count,NULL);

	while(1)
	{
		//读取文件里面的内容
		read(fd,&buf,sizeof(buf));

		//分析数据

		switch(buf.type)//分析是什么事件
		{
			case EV_KEY:
				{
					if(buf.value == 0)
					{
						printf("屏幕松开\r\n");
						if(temp1[0]<400)//判断按下是左半屏幕还是右
						{
							touch =2;//上一张
						}else//点的是右半屏幕
						{
							touch =1;//下一张
						}
						timer=0;//从新计时
						flag=1;//多点触发会有其他数据 ，这样可以屏蔽掉
					}
					break;
				}
			case EV_ABS:
				{
					if(flag)
					{
						switch(buf.code)
						{
							case ABS_MT_POSITION_X:
								{
								//	printf("x=%d\r\n",buf.value);
									temp1[0]=buf.value;//读取当前按下x轴位置
									temp[0]=buf.value;//防止进入滑动显示的时候还会从刷一次
									break;
								}
							case ABS_MT_POSITION_Y:
								{
								//	printf("y=%d\r\n",buf.value);
									temp1[1]=buf.value;//和x轴相同
									temp[1]=buf.value;
									break;
								}
							case ABS_MT_TRACKING_ID:
								{
								//	printf("id=%d\r\n",buf.value);
									flag=0;//读取完一次完整坐标之后，屏蔽其他触点
									break;
								}
						}
					}else
					{
						if(timer > 50)//判断是不是滑动
						{
							hua_read(fd,0);
							if((temp1[0]-temp[0])>0)//判断滑动方向
							{
								touch = 1;
							}else if((temp1[0]-temp[0]) < 0)
							{
								touch =2;
							}else//不滑动就看起始点位置
							{
								if(temp[0] < 400)
								{
									touch = 2;
								}else
								{
									touch = 1;
								}
							}

							timer=0;//从新计时
							flag = 1;//从新读取新的点位
						}
					}

					break;
				}
			case EV_SYN:
				{
					break;
				}
		}

		//显示获取到的数据
	}

	//关闭文件
	close(fd);
}


