CC = gcc
CFLAGS = -Wall -Werror -fPIC -Wvla -g

all: sound_seg.o

sound_seg.o: sound_seg.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg.o

clean:
	rm -f sound_seg.o
