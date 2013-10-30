#ifndef AVL_H_
#define AVL_H_

#include <pthread.h>
#include <stdint.h>

class VAddressRange;

// A balanced binary search tree.
typedef struct avl_tree_s {
  // R/W lock to protect the tree.
  pthread_rwlock_t rwlock;
  // Root of this tree. It's actually an AVLNode.
  void* tree;
  int num_nodes;
} AVLTree;

// a tree node of the AVL tree.
typedef struct AVLNode_s {
  // AVL tree management.
  struct AVLNode_s* left;
  struct AVLNode_s* right;
  unsigned int height;

  // key to identify a node
  uint64_t address;
  uint64_t len;
  // This tree-node is embedded into this object.
  VAddressRange *embedding_object;
} AVLNode;

// Traverse the BST layer by layer, and put all tree nodes in the
// array q[].
// q[] size is "qlen".  "lvl" will be set to tree height.
// Return number of tree nodes.
int level_traverse_avl_tree(AVLTree* avl, AVLNode* q[],
                            int qlen, int* lvl);

void dump_avl_node(AVLNode* node);

void dump_avl_tree(AVLTree* avl);

// Dump all nodes in the BST.
void get_all_avl_nodes(AVLTree* avl);

// Init an BST.
void InitAVL(AVLTree* avl);

// Destory an BST.
void DestoryAVL(AVLTree* avl);

// Insert a new node into the BST.
int InsertNode(AVLTree *avl, AVLNode* newnode);

// Remove an existing node from BST.
void DeleteNode(AVLTree *avl, AVLNode* node);

// Find a node such that, (key) is within the range [node->address, length).
AVLNode* FindNode(AVLTree *avl, uint64_t key);

#endif  // AVL_H__
