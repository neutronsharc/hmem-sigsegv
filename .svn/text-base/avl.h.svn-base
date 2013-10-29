#ifndef __AVL_H__
#define __AVL_H__


#include <pthread.h>



/*
a balanced bst
*/
typedef struct avl_tree_s
{
    pthread_rwlock_t    rwlock;

    void* tree;
    int num_nodes;

}avl_tree_t;


/*
a tree node of the AVL tree.
*/
typedef struct avl_node_s
{
    //// AVL tree management.
    struct avl_node_s *left;
    struct avl_node_s *right;
    unsigned int height;

    //// key to identify a node
    unsigned long address;
    unsigned long len;

    ///  More options:  User handler.
    //void *handler_arg;
}avl_node_t;

int level_traverse_avl_tree(avl_tree_t* avl, avl_node_t *q[], int qlen, int *lvl);


int insert( avl_tree_t *avl, avl_node_t* newnode);
void    delete(avl_tree_t *avl, avl_node_t* node);
avl_node_t* find(avl_tree_t *avl, unsigned long key);




void    dump_avl_node(avl_node_t* node);



#endif // __AVL_H__
