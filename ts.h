#ifndef _TS_H_
#define _TS_H_


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define TSPATH "/dev/input/event3"

extern int touch;


void ts_read(char *tspath);

#endif


