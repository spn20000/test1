#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>


#define SCREN "/dev/fb0"
#define WIDE 800
#define HIGH 1280

typedef struct _test{
	int set_x;
	int set_y;

	int size_x;
	int size_y;
	unsigned int color;
}KEY;

KEY key[]={
	[0]={
		.set_x = 0,
		.set_y = 0,
		.size_x = 200,
		.size_y = 120,
		.color = 0xFF0000,
	},
	[1]={

	},

};



//屏幕划出一个按键
int main(int argc,char *argv[])
{

	//打开屏幕文件
	int fd = open(SCREN, O_RDWR);
	if(-1 == fd)
	{
		perror("open error\r\n");
		return 0;
	}

	//映射
	int *addr =(int *)mmap(NULL, WIDE*HIGH*4, PROT_WRITE, MAP_SHARED,fd, 0);
	if((int *)-1 == addr)
	{
		perror("mmap error\r\n");
		return 0;
	}

	//在需要的内容点位置划出区域
	for(int i=0;i<key[0].size_y;i++)//按键有多少行
	{
		for(int j=0;j<key[0].size_x;j++)//一行多少个像素点
		{
			*(addr+(key[0].set_x+j)+WIDE*(i+key[0].set_y)) = key[0].color;
		}
	}

	//取消映射
	if(munmap(addr, WIDE*HIGH*4))
	{
		perror("munmap error\r\n");
		return 0;
	}

	//关闭文件
	if(close(fd))
	{
		perror("close error\r\n");
		return 0;
	}
	return 0;
}



