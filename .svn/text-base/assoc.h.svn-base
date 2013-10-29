#ifndef __ASSOC_H__
#define __ASSOC_H__

void    dump_hash_stats();
void    clear_hash_stats();


/* associative array */
void assoc_init(int tbl_bits);
void    assoc_destroy(void);

//item *assoc_find(const char *key, const size_t nkey);
item *assoc_find(const void *key, const size_t nkey);
int assoc_insert(item *item);

//void assoc_delete(const *key, const size_t nkey);
void assoc_delete(const void *key, const size_t nkey);

void do_assoc_move_next_bucket(void);

int start_assoc_maintenance_thread(void);
void stop_assoc_maintenance_thread(void);

#endif //#ifndef __ASSOC_H__
