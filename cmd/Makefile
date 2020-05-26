CONFIG_MODULE_SIG=n
CC = gcc
CFLAGS = -g -Wall
all:
	$(CC) $(CFLAGS) -o mkfs.episode mkfs.episode.c blkdev.c linux_version.c
clean:
	rm -rf mkfs.episode
