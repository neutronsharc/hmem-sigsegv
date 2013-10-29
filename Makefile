
CC = gcc -g

INC = -I/home/ouyangx/tools/install-libevent-2.0.10/include/ -I.
#-I/home/ouyangx/memcached/install-libmemcached-0.44/include 
CFLAGS = -c ${INC}
LDFLAGS = -lpthread -pthread -lrt
#-L/home/ouyangx/memcached/install-libmemcached-0.44/lib -lmemcached

##exe = test mmap sigseg vaddr
exe = sigseg vaddr scan

objs = test.o hash.o  assoc.o slabs.o items.o vaddr.o sigseg.o avl.o test-vaddr.o
#assoc.c slabs.o  item.o

all : ${exe}

scan : scan.o vaddr.o sigseg.o avl.o hash.o  assoc.o slabs.o items.o ram_release.o
	${CC} ${INC} ${LDFLAGS} $^ -o $@ -lrt

test : ${objs}
	${CC} ${LDFLAGS} $^ -o $@

mmap : mmap.c
	${CC} ${INC} ${LDFLAGS} $^ -o $@

sigseg : test-sigseg.o
	${CC} ${INC} ${LDFLAGS} $^ -o $@

vaddr : test-vaddr.o vaddr.o sigseg.o avl.o hash.o  assoc.o slabs.o items.o ram_release.o
	${CC} ${INC} ${LDFLAGS} $^ -o $@


%.o: %.c *.h 
	$(CC) ${CFLAGS} -c $< -o $@

##.c.o:
##	${CC} ${CFLAGS} $^

clean :
	rm -f ${exe} ${objs}


