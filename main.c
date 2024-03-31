#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include "ts.h"

#define WIDE 800
#define HIGH 1280
#define LCDPATH "/dev/fb0"


typedef struct test{
	//前后的指针
	struct test *next;
	struct test *last;

	//主要内容
	char name[20];

}TEST;



//创建链表头
TEST *create_head(void)
{
	TEST *p = (TEST *)malloc(sizeof(TEST));
	memset(p,0,sizeof(TEST));
	p->next = NULL;
	p->last = NULL;
	return p;
}


//创建设备节点
void create_port(TEST *head,char *name)
{
	static int flag = 1;
	TEST *p=(TEST *)malloc(sizeof(TEST));
	memset(p,0,sizeof(TEST));
	if(flag)
	{
		flag=0;
		//创建第一个节点
		head->next = p;
		p->next=p;
		p->last=p;
	}else
	{
		//尾插法
		TEST *w=head->next->last;//尾部节点
		//不是第一次的节点
		p->next=head->next;
		p->last=w;

		w->next = p;
		head->next->last=p;
	}
	strcpy(p->name,name);		
}


//释放堆区空间
void free_head(TEST *head)
{
	//断开循环
	head->next->last->next=NULL;
	TEST *p=NULL;;
	while(head->next)//最后一项
	{
		//保存下一个节点
		p=head->next;

		//释放上一个节点
		free(head);

		//指向下一个节点
		head=p;
	}

	free(head);
}



//线程处理函数
void *read_scren(void *arg)
{
	char *path = (void *)arg;
	ts_read(path);	
}


//显示一张任意bmp图片(不超过屏幕范围)
int main(int argc,char *argv[])
{
	//变量
	char buf[WIDE*HIGH*3 + 54]={0};//用来储存读取到的图片数据
	int pic[WIDE*HIGH] = {0};//用来储存整理之后的数据
	TEST *head = create_head( );

	//0、默认路径  图片所在地
	char *path = ".";
	if(argv[1])
	{
		path = argv[1];
	}

	char *tspath=TSPATH;
	if(argc == 3)
	{
		tspath = argv[2];
	}

	//使用标注管道 读取文件名
	char pathname[100]={0};
	sprintf(pathname,"ls %s",path);
	FILE *fp = popen(pathname,"r");
	if(NULL == fp)
	{
		perror("popen error\r\n");
		return 0;
	}

	
	char temp[20]={0};//储存读到的文件名
	char test[40]={0};
	char arg[20]={0};
	strcpy(arg,argv[1]);
	while(1)
	{
		if(fgets(temp,sizeof(temp),fp) == NULL)
		{
			break;
		}

		if(strncmp(".bmp\n",temp+strlen(temp)-5,5) == 0)
		{
			temp[strlen(temp)-1]='\0';
			if(arg[strlen(arg)-1] == '/')
			{
				arg[strlen(arg)-1]='\0';
			}
			sprintf(test,"%s/%s",arg,temp);
			create_port(head,test);
		}

		memset(temp,0,20);
	}

	if(pclose(fp) == -1)
	{
		perror("pclose error\r\n");
		return 0;
	}


	//1、打开文件  open  屏幕设备
	int fd1 = open(LCDPATH, O_RDWR);
	if(-1 == fd1)
	{
		perror("open error\r\n");
		return 0;
	}

	//2、映射物理地址  mmap
	int *addr = mmap(NULL, WIDE*HIGH*4, PROT_WRITE, MAP_SHARED,fd1, 0);
	if((int *)-1 == addr)
	{
		perror("mmap error\r\n");
		return 0;
	}

	//创建线程去读取触摸屏
	pthread_t tid = 0;
	pthread_create(&tid,NULL,read_scren,(void *)tspath);

	TEST *p=head->next;
	char choose;
	touch = 3;
	while(1)//循环控制图片
	{
		if(touch)
		{
			if(touch == 1)
			{
				p=p->next;
			}else if(touch ==2)
			{
				p=p->last;
			}
			touch = 0;

			int fd2 = open(p->name, O_RDWR);
			if(-1 == fd1)
			{
				perror("open error\r\n");
				return 0;
			}

			//2、读取图片的所有数据 read
			if(read(fd2, buf, sizeof(buf)) == -1)
			{
				perror("read error\r\n");
				return 0;
			}
			//18~21  图片的宽度    22~25 图片的高度
			//int x = buf[18] << 0 | buf[19] << 8 | buf[20] << 16 | buf[21] << 24;
			int wide=*(int *)(&buf[18]);
			printf("wide=%d\r\n",wide);
			int high=*(int *)(&buf[22]);
			printf("high=%d\r\n",high);
			int x=(WIDE-wide)/2;
			int y=(HIGH-high)/2;

			//3、转换图片读取到的数据
			for(int i=0;i<high;i++)
			{
				for(int j=0;j<wide;j++)
				{
					pic[j+i*wide]=buf[54+3*(j+(high-i-1)*wide)+0] | buf[54+3*(j+(high-i-1)*wide)+1] << 8 | buf[54+3*(j+(high-i-1)*wide)+2]<<16;
				}
			}

			//5、写入内容到映射地址 
			for(int i=0;i<HIGH;i++)//行数
			{
				for(int j=0;j<WIDE;j++)//列数
				{
					if(j >= x && j < x+wide && i >= y && i < y+high)
					{
						*(addr+j+i*WIDE) = pic[j-x + (i-y)*wide]; 
					}else
					{
						*(addr+j+i*WIDE) = 0;
					}
				}
			}

			if(close(fd2) == -1)
			{
				perror("close error\r\n");
				return 0;
			}
		}
	}
	//6、取消映射munmap
	if(munmap(addr, WIDE*HIGH*4) == -1)
	{
		perror("munmap error\r\n");
		return 0;
	}

	//7、关闭文件 close
	if(close(fd1) == -1)
	{
		perror("close error\r\n");
		return 0;
	}

	return 0;
}
