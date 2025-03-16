all: initsocket user1 user2

ksocket.o: ksocket.c ksocket.h
	gcc -Wall -g -c ksocket.c -o ksocket.o

initsocket: ksocket.o
	gcc -Wall -g initsocket.c ksocket.o -lpthread -o initsocket

user1: ksocket.o
	gcc -Wall -g user1.c ksocket.o -lpthread -o user1

user2: ksocket.o
	gcc -Wall -g user2.c ksocket.o -lpthread -o user2

clean:
	rm -f *.o initsocket user1 user2

.PHONY: all clean
