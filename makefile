all: manager feed

manager: manager.o
	gcc -o manager manager.o -pthread

manager.o: manager.c communication.h
	gcc -c manager.c


feed: feed.o
	gcc -o feed feed.o -lpthread

feed.o: feed.c communication.h
	gcc -c feed.c

clean:
	rm -f manager feed manager.o feed.o

clean-manager:
	rm -f manager manager.o

clean-feed:
	rm -f feed feed.o	



