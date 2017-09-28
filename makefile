CC=gcc

all:bms ser

bms:bms.c
	$(CC) -o $@ $^

ser:epoll_ser.c
	$(CC) -o $@ $^

clean:
	rm -rf ser bms

