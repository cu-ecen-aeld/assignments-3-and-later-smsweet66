all: aesdsocket

aesdsocket: aesdsocket.o connection_info.o timestamp_writer.o
	${CC} ${LDFLAGS} aesdsocket.o connection_info.o timestamp_writer.o -o aesdsocket

aesdsocket.o: aesdsocket.c
	${CC} ${CCFLAGS} -c aesdsocket.c

connection_info.o: connection_info.c
	${CC} ${CCFLAGS} -c connection_info.c

timestamp_writer.o: timestamp_writer.c
	${CC} ${CCFLAGS} -c timestamp_writer.c

debug: CCFLAGS += -DDEBUG -g
debug: aesdsocket

clean:
	rm -f *.o aesdsocket
