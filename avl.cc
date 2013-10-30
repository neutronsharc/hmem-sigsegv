// A balanced BST:  AVL tree

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "avl.h"
#include "debug.h"

#define empty  ((AVLNode *) 0)
#define heightof(tree)  ((tree) == empty ? 0 : (tree)->height)
// a balanced BST height won't exceed 41 = 2^41 nodes
#define MAXHEIGHT 41

void dump_avl_node(AVLNode* node) {
  if (!node) {
    return;
  }
  fprintf(stderr, "[%lx, %lx] ", node->address, node->address + node->len - 1);
}

int level_traverse_avl_tree(AVLTree* avl, AVLNode* q[], int qlen, int* lvl) {
  int front = 0, end = 0;  // queue ptr.
  int cnt = 0;             // node counter.
  int currLvl = 0, nextLvl = 0; // nodes in current and next levels.
  int lvlCnt = 0;  // count num of levels

  AVLNode* node = (AVLNode*)avl->tree;
  if (!node) {
    err("tree is empty\n");
    return 0;
  }

  // Enqueue the root node.
  q[end++] = node;
  cnt++;
  currLvl++;
  q[end++] = (AVLNode*)0xf;  // level end delimiter

  while (front < end) {
    node = q[front++];
    if (node == (AVLNode*)0xf) {
      continue;
    }
    currLvl--;

    if (node->left) {
      q[end++] = node->left;
      cnt++;
      nextLvl++;
    }
    if (node->right) {
      q[end++] = node->right;
      cnt++;
      nextLvl++;
    }
    if (currLvl == 0) {  // has finished curr level
      currLvl = nextLvl;
      nextLvl = 0;
      q[end++] = (AVLNode*)0xf;  // level end delimiter
      lvlCnt++;
    }
  }
  printf("\nIn total %d nodes, %d lvls\n", cnt, lvlCnt);
  *lvl = lvlCnt;
  return cnt;
}

void get_all_avl_nodes(AVLTree* avl) {
  int len = 1024;
  AVLNode* q[len];
  AVLNode* node;
  int lvlCnt = 0;
  int i = 0, lvl = 0;
  int cnt = 0;

  lvl = 1;
  cnt = level_traverse_avl_tree(avl, q, 1024, &lvlCnt);
  printf("\nLvl-%2d: ", lvl);
  for (i = 0; i < cnt + lvlCnt; i++) {
    node = q[i];
    if (node == (void*)0xf) {
      printf("\n\nLvl-%2d: ", ++lvl);
      continue;
    }
    dump_avl_node(node);
  }
  printf("\n\n");
}

void dump_avl_tree(AVLTree* avl) {
  int cnt = 0;
  AVLNode** q;
  int front = 0, end = 0;
  int currLvl = 0, nextLvl = 0;

  AVLNode* node = (AVLNode*)avl->tree;
  if (!node) {
    printf("tree is empty...\n");
    return;
  }

  int flag = 0;
  int lvlCnt = 1;
  q = (AVLNode**)malloc(1000 * sizeof(AVLNode*));
  q[end++] = node;
  cnt++;
  currLvl = 1;
  flag = 1;

  while (front < end) {
    node = q[front++];
    currLvl--;
    if (flag) {
      printf("Lvl=%2d:: ", lvlCnt);
      flag = 0;
    }
    dump_avl_node(node);

    if (node->left) {
      q[end++] = node->left;
      cnt++;
      nextLvl++;
    }
    if (node->right) {
      q[end++] = node->right;
      cnt++;
      nextLvl++;
    }

    if (currLvl == 0) {  // has finished curr level
      currLvl = nextLvl;
      nextLvl = 0;
      printf("\n");
      flag = 1;
      lvlCnt++;
    }
  }
  printf("\nIn total %d nodes, %d lvls\n", cnt, lvlCnt - 1);
  free(q);
}

static void avl_rebalance(AVLNode*** nodeplaces_ptr, unsigned int count) {
  if (count > 0)
    do {
      AVLNode** nodeplace = *--nodeplaces_ptr;
      AVLNode* node = *nodeplace;
      AVLNode* nodeleft = node->left;
      AVLNode* noderight = node->right;
      unsigned int heightleft = heightof(nodeleft);
      unsigned int heightright = heightof(noderight);
      if (heightright + 1 < heightleft) {
        AVLNode* nodeleftleft = nodeleft->left;
        AVLNode* nodeleftright = nodeleft->right;
        unsigned int heightleftright = heightof(nodeleftright);
        if (heightof(nodeleftleft) >= heightleftright) {
          node->left = nodeleftright;
          nodeleft->right = node;
          nodeleft->height = 1 + (node->height = 1 + heightleftright);
          *nodeplace = nodeleft;
        } else {
          nodeleft->right = nodeleftright->left;
          node->left = nodeleftright->right;
          nodeleftright->left = nodeleft;
          nodeleftright->right = node;
          nodeleft->height = node->height = heightleftright;
          nodeleftright->height = heightleft;
          *nodeplace = nodeleftright;
        }
      } else if (heightleft + 1 < heightright) {
        AVLNode* noderightright = noderight->right;
        AVLNode* noderightleft = noderight->left;
        unsigned int heightrightleft = heightof(noderightleft);
        if (heightof(noderightright) >= heightrightleft) {
          node->right = noderightleft;
          noderight->left = node;
          noderight->height = 1 + (node->height = 1 + heightrightleft);
          *nodeplace = noderight;
        } else {
          noderight->left = noderightleft->right;
          node->right = noderightleft->left;
          noderightleft->right = noderight;
          noderightleft->left = node;
          noderight->height = node->height = heightrightleft;
          noderightleft->height = heightright;
          *nodeplace = noderightleft;
        }
      } else {
        unsigned int height =
            (heightleft < heightright ? heightright : heightleft) + 1;
        if (height == node->height)
          break;
        node->height = height;
      }
    } while (--count > 0);
}

static AVLNode* avl_insert(AVLNode* new_node, AVLNode* tree) {
  unsigned long key = new_node->address;
  AVLNode** nodeplace = &tree;
  AVLNode** stack[MAXHEIGHT];
  unsigned int stack_count = 0;
  AVLNode*** stack_ptr = &stack[0];
  for (;;) {
    AVLNode* node = *nodeplace;
    if (node == empty)
      break;
    *stack_ptr++ = nodeplace;
    stack_count++;
    if (key < node->address)
      nodeplace = &node->left;
    else
      nodeplace = &node->right;
  }
  new_node->left = empty;
  new_node->right = empty;
  new_node->height = 1;
  *nodeplace = new_node;
  avl_rebalance(stack_ptr, stack_count);
  return tree;
}

static AVLNode* avl_delete(AVLNode* node_to_delete, AVLNode* tree) {
  unsigned long key = node_to_delete->address;
  AVLNode** nodeplace = &tree;
  AVLNode** stack[MAXHEIGHT];
  unsigned int stack_count = 0;
  AVLNode*** stack_ptr = &stack[0];
  for (;;) {
    AVLNode* node = *nodeplace;
    if (node == empty)
      return tree;
    *stack_ptr++ = nodeplace;
    stack_count++;
    if (key == node->address) {
      if (node != node_to_delete)
        abort();
      break;
    }
    if (key < node->address)
      nodeplace = &node->left;
    else
      nodeplace = &node->right;
  }
  {
    AVLNode** nodeplace_to_delete = nodeplace;
    if (node_to_delete->left == empty) {
      *nodeplace_to_delete = node_to_delete->right;
      stack_ptr--;
      stack_count--;
    } else {
      AVLNode*** stack_ptr_to_delete = stack_ptr;
      AVLNode** nodeplace = &node_to_delete->left;
      AVLNode* node;
      for (;;) {
        node = *nodeplace;
        if (node->right == empty)
          break;
        *stack_ptr++ = nodeplace;
        stack_count++;
        nodeplace = &node->right;
      }
      *nodeplace = node->left;
      node->left = node_to_delete->left;
      node->right = node_to_delete->right;
      node->height = node_to_delete->height;
      *nodeplace_to_delete = node;
      *stack_ptr_to_delete = &node->left;
    }
  }
  avl_rebalance(stack_ptr, stack_count);
  return tree;
}

void InitAVL(AVLTree* avl) {
  avl->tree = empty;
  avl->num_nodes = 0;
  pthread_rwlock_init(&avl->rwlock, NULL);
}

void DestoryAVL(AVLTree* avl) {
  pthread_rwlock_destroy(&avl->rwlock);
}

int InsertNode(AVLTree* avl, AVLNode* newnode) {
  if (!newnode || newnode->len < 1) {
    err("invalid node...\n");
    return -1;
  }

  pthread_rwlock_wrlock(&avl->rwlock);

  avl->tree = avl_insert(newnode, (AVLNode*)avl->tree);
  avl->num_nodes++;

  pthread_rwlock_unlock(&avl->rwlock);
  return avl->num_nodes;
}

void DeleteNode(AVLTree* avl, AVLNode* node) {
  if (!node) {
    return;
  }
  pthread_rwlock_wrlock(&avl->rwlock);
  avl->tree = avl_delete(node, (AVLNode*)avl->tree);
  avl->num_nodes--;
  pthread_rwlock_unlock(&avl->rwlock);
}

AVLNode* FindNode(AVLTree* avl, uint64_t key) {
  AVLNode* nd = (AVLNode*)avl->tree;
  pthread_rwlock_rdlock(&avl->rwlock);
  while (1) {
    if (!nd) {
      pthread_rwlock_unlock(&avl->rwlock);
      return (AVLNode*)NULL;
    }
    if (key < nd->address) {
      nd = nd->left;
    } else if (key >= (nd->address + nd->len)) {
      nd = nd->right;
    } else {
      pthread_rwlock_unlock(&avl->rwlock);
      return nd;
    }
  }
  pthread_rwlock_unlock(&avl->rwlock);
  return (AVLNode*)NULL;
}
