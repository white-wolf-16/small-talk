s-talk: main.c  list.h list.o
	gcc -g -o s-talk list.h list.o main.c -pthread

clean:	
	rm -f s-talk s-talk.o 

