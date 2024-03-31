obj-m += gt9271_drv.o gt9271_client.o

#获取原码路径
KID=/home/l/rk3399/kernel


#创建目标项
all:
	make ARCHV=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C ${KID} M=${PWD} modules
	#@aarch64-linux-gnu-gcc app.c -o app
	@rm *odule* *mod.c *.o
#make  使用命令      -C ${KID}  -C指定对应路径下的Makefile文件       
#M=${PWD}  选择存储的位置在当前面目录下          			modules编译成模块
#使用原码顶层的Makefil文件编译obj-m的文件，把编译好的文件存放在当前目录

clear:
	rm *.ko


