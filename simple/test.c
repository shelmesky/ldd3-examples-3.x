#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <error.h>
#include <errno.h>

// BIOS ROM
#define SIZE 0x10000
#define OFFSET 0xf0000

// Video ROM
//#define SIZE 32767
//#define OFFSET 0x000c0000

// ACPI Tables
//#define SIZE 65535
//#define OFFSET 0x2fff0000

int main(int argc, char *argv[])
{
	int fdin, fdout;
	void *src, *dst;
	struct stat statbuf;
	unsigned char sz[SIZE]={0};
	/*
	if ((fdin = open("/dev/simple_fault", O_RDWR|O_SYNC)) < 0)
		perror("can't open /dev/simple_fault for reading");
	*/
	if ((fdin = open("/dev/simple_remap", O_RDWR|O_SYNC)) < 0)
		perror("can't open /dev/simple_remap for reading");

	if ((src = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
					fdin, OFFSET)) == MAP_FAILED)
		perror("mmap error for simplen");

	memcpy(sz, src, SIZE);

	int i;
	for (i=0; i<SIZE; i++) {
		printf("%c", sz[i]);
	}

	munmap(src, SIZE);
	close(fdin);
	exit(0);
}
