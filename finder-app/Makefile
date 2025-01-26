all: writer

writer: writer.o
	${CC} ${LDFLAGS} writer.o -o writer

writer.o: writer.c
	${CC} ${CCFLAGS} -c writer.c 

clean:
	rm -f writer.o writer
