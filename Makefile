#CC = g++ -O3
CC = g++ -g

INC = -I.
CFLAGS = -c ${INC}
LDFLAGS = -lpthread -pthread -lrt -laio

exes = newhmem file_throughput_test write_file

tests = vaddr_range_test hash_table_test free_list_test lru_list_test bitmap_test \
	page_allocation_table_test page_stats_table_test asyncio_test

objs = hybrid_memory.o page_cache.o avl.o vaddr_range.o sigsegv_handler.o \
	utils.o hash.o ram_cache.o hybrid_memory_lib.o page_allocation_table.o \
	page_stats_table.o flash_cache.o asyncio_manager.o asyncio_request.o

all : ${exes} ${tests}

newhmem : hybrid_memory_test.o ${objs}
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

vaddr_range_test : vaddr_range_test.o ${objs}
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

hash_table_test : hash_table_test.o  hash_table.h hash.o
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

free_list_test : free_list_test.o free_list.h utils.o
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

lru_list_test : lru_list_test.o lru_list.h
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

bitmap_test : bitmap_test.o bitmap.h
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

page_allocation_table_test : page_allocation_table_test.o page_allocation_table.o
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

page_stats_table_test : page_stats_table_test.o page_stats_table.o
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

file_throughput_test : file_throughput_test.cc
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

asyncio_test : asyncio_test.o asyncio_manager.o asyncio_request.o utils.o
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

write_file : write_file.cc
	${CC} ${INC} $^ -o $@ ${LDFLAGS}

%.o: %.cc *.h
	$(CC) ${CFLAGS} -c $< -o $@

.c.o:
	$(CC) ${CFLAGS} -c $< -o $@

clean :
	rm -f *.o ${exes} ${objs} ${tests} *.gch a.out *.swp
