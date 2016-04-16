#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <error.h>
#include <errno.h>

#define SIZE 4096
#define OFFSET 0x00000000
//#define VERSION "remap"
#define VERSION "fault"

int main(int argc, char *argv[])
{
	int fdin, fdout;
	void *src, *dst;
	struct stat statbuf;
	unsigned char sz[SIZE]={0};

	if (VERSION == "fault") {
	    if ((fdin = open("/dev/simple_fault", O_RDWR|O_SYNC)) < 0)
		    perror("can't open /dev/simple_fault for reading");
	} else if (VERSION == "remap") {	
	    if ((fdin = open("/dev/simple_remap", O_RDWR|O_SYNC)) < 0)
		    perror("can't open /dev/simple_remap for reading");
	} else {
	    printf("bad version\n");
	    exit(1);
	}

	if ((src = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
					fdin, OFFSET)) == MAP_FAILED)
		perror("mmap error for simplen");

	memcpy(sz, src, SIZE);
	printf("%s\n", sz);

	munmap(src, SIZE);
	close(fdin);
	exit(0);
}
