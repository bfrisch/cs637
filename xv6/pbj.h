#ifndef __PBJ_H_
#define __PBJ_H_

#include "buf.h"
#define NBPT 20

// File system journal super block
struct jsblock {
  uint size;         // Size of entire journal (blocks)
  uint head_block;   // Number of the block in whole FS with head of journal
  uint tail_block;   // Number of the block in whole FS with tail of journal
  uint time_stamp;
};

struct jtrans {
  uint checksum;
  uint num_data_blocks;
  uint blocklist[NBPT];
};

struct t_data_node {
  struct buf* item;
  struct t_data_node* next;
};

struct mem_trans {
  struct t_data_node* bufs;
  uint size;
};

// wraps a transaction
struct mem_trans_node {
  struct mem_trans* trans;
  struct mem_trans_node* next;
};

void end_trans(void);
extern int should_panic;

#endif
