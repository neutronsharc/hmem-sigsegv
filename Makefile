#CC = g++ -O3
CC = g++ -g

INC = -I. -I/home/ouyangx/tools/install-libevent-2.0.10/include/
#-I/home/ouyangx/memcached/install-libmemcached-0.44/include 
CFLAGS = -c ${INC}
LDFLAGS = -lpthread -pthread -lrt

exe = newhmem vaddr_range_test

objs = hybrid_memory.o page_cache.o avl.o vaddr_range.o sigsegv_handler.o utils.o

all : ${exe}

newhmem : hybrid_memory_test.o ${objs}
	${CC} ${INC} $^ -o $@ ${LDFLAGS} 

vaddr_range_test : vaddr_range_test.o ${objs}
	${CC} ${INC} $^ -o $@ ${LDFLAGS} 

%.o: %.cc *.h 
	$(CC) ${CFLAGS} -c $< -o $@

##.c.o:
##	${CC} ${CFLAGS} $^

clean :
	rm -f *.o ${exe} ${objs}
