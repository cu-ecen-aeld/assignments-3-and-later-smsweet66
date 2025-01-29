all: aesdsocket

aesdsocket: aesdsocket.o
	${CC} ${LDFLAGS} aesdsocket.o -o aesdsocket

aesdsocket.o: aesdsocket.c
	${CC} ${CCFLAGS} -c aesdsocket.c 

clean:
	rm -f aesdsocket.o aesdsocket
