all: mainwth.c
	gcc mainwth.c -o mainwth -pthread
	./mainwth
clean:
	rm mainwth