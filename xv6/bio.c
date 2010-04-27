// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
// 
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to flush it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
// 
// The implementation uses three state flags internally:
// * B_BUSY: the block has been returned from bread
//     and has not been passed back to brelse.  
// * B_VALID: the buffer data has been initialized
//     with the associated disk block contents.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "pbj.h"
#include "fs.h"
#include "x86.h"

struct buf buf[NBUF];
struct spinlock buf_table_lock;
struct spinlock trans_write_lock;
struct spinlock jcache_lock;

// Linked list of all buffers, through prev/next.
// bufhead->next is most recently used.
// bufhead->tail is least recently used.
struct buf bufhead;
struct buf jtail_buf;
struct jtrans jt;
struct buf jtrans_header_buf;

struct buf* jsblock_buf;
struct jsblock* jsb;

uint jtail_bnum = 3;

struct mem_trans_node* mtn_head;
struct mem_trans_node* mtn_tail;

void
binit(void)
{
  struct buf *b;

  initlock(&buf_table_lock, "buf_table");
  initlock(&trans_write_lock, "trans_write");
  initlock(&jcache_lock, "jcache");
  cprintf("Blocks inited!");
  // Create linked list of buffers
  bufhead.prev = &bufhead;
  bufhead.next = &bufhead;
  for(b = buf; b < buf+NBUF; b++){
    b->next = bufhead.next;
    b->prev = &bufhead;
    bufhead.next->prev = b;
    bufhead.next = b;
  }
}

int
bcheck(uint dev, uint sector) {
  // Return 1 if the block is in the cache, otherwise
  //   return 0.

  struct buf *b;

  acquire(&buf_table_lock);
  for(b = bufhead.next; b != &bufhead; b = b->next){
    if((b->flags & (B_BUSY|B_VALID)) &&
       b->dev == dev && b->sector == sector){
      release(&buf_table_lock);
      return 1;
    }
  }
  release(&buf_table_lock);
  return 0;
}

// Look through buffer cache for sector on device dev.
// If not found, allocate fresh block.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint sector)
{
  struct buf *b;
  acquire(&buf_table_lock);

 loop:
  // Try for cached block.
  for(b = bufhead.next; b != &bufhead; b = b->next){
    if((b->flags & (B_BUSY|B_VALID)) &&
       b->dev == dev && b->sector == sector){
      if(b->flags & B_BUSY){
        sleep(buf, &buf_table_lock);
        goto loop;
      }
      b->flags |= B_BUSY;
      release(&buf_table_lock);
      return b;
    }
  }

  // Allocate fresh block.
  for(b = bufhead.prev; b != &bufhead; b = b->prev){
    if((b->flags & B_BUSY) == 0){
      b->flags = B_BUSY;
      b->dev = dev;
      b->sector = sector;
      release(&buf_table_lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a B_BUSY buf with the contents of the indicated disk sector.
struct buf*
bread(uint dev, uint sector)
{
  struct buf *b;

  b = bget(dev, sector);
  if(!(b->flags & B_VALID))
    ide_rw(b);
  return b;
}

// Write buf's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if((b->flags & B_BUSY) == 0)
    panic("bwrite");
  b->flags |= B_DIRTY;

  // Add the just changed block to the top of the buffer list for the current transaction.
  struct t_data_node* buf_item = (struct t_data_node*) kmalloc(sizeof(struct t_data_node));
  buf_item->item = b;
  struct mem_trans* ct = curmt();
  buf_item->next = ct->bufs;
  ct->bufs = buf_item;
  ct->size++;
}

// Assuming the journal transaction write lock is acquired
void jbwrite(struct buf *b) {
  uint orig_sector = b->sector;
  b->sector = jtail_bnum;
  b->flags |= B_DIRTY;
  ide_rw(b);
  b->sector = orig_sector;
  b->flags |= B_BUSY;
  jtail_bnum++;
  if (jtail_bnum > jsb->size + 2) {
    jtail_bnum = 3;
  }
}


// Release the buffer buf.
void
jbrelse(struct buf *b)
{
  if((b->flags & B_BUSY) == 0)
    panic("brelse");

  acquire(&buf_table_lock);

  b->next->prev = b->prev;
  b->prev->next = b->next;
  b->next = bufhead.next;
  b->prev = &bufhead;
  bufhead.next->prev = b;
  bufhead.next = b;

  b->flags &= ~B_BUSY;
  wakeup(buf);

  release(&buf_table_lock);
}

void jfull_flush() {
 
  struct mem_trans_node* curr_mtn;
  struct mem_trans_node* curr_tail = mtn_tail;
  struct t_data_node* curr_buf;
  
  for (curr_mtn = mtn_head; curr_mtn != curr_tail->next; curr_mtn = curr_mtn->next) 
    {
    for (curr_buf = curr_mtn->trans->bufs; curr_buf; curr_buf = curr_buf->next) 
    {
      cprintf("Writing buf sector %d!\n", curr_buf->item->sector);
      ide_rw(curr_buf->item);
      jbrelse(curr_buf->item);
    }
  }
 
  acquire(&jcache_lock);
  mtn_head = curr_tail->next;
  release(&jcache_lock);
  cprintf("Z");
}

void
pbjFSck(uint dev) {
  // Init head and tail...
  cprintf("Reading uber block!\n");
  jsblock_buf = bread(dev, 2); // read journal uber block
  jsb = (struct jsblock*) &(jsblock_buf->data);
  cprintf("Journal is of size: %d!\n", jsb->size);
  struct buf** journal_blocks = (struct buf**) kmalloc(sizeof(struct buf*) * jsb->size);  // memory for entire journal
  int bnum;
  for (bnum = 0; bnum < jsb->size; bnum++) {
    journal_blocks[bnum] = bread(dev, bnum + 3); // read entire journal
  }

  if (jsb->head_block < jsb->tail_block) {
    cprintf("Head is less then tail\n");
    bnum = jsb->head_block - 3; // current transaction header block
    while (bnum < jsb->tail_block - 3) {
      // Read the transaction header block
      struct jtrans* curr_jtrans = (struct jtrans*) &(journal_blocks[bnum]->data);

      // Setup the transaction.
      struct mem_trans* mt = (struct mem_trans*) kmalloc(sizeof(struct mem_trans));
      mt->size = curr_jtrans->num_data_blocks;
      
      // Make the head t_data_node;
      struct t_data_node* curr_tail = (struct t_data_node*) kmalloc(sizeof(struct t_data_node));
      curr_tail->item = journal_blocks[bnum+1];
      curr_tail->item->sector = curr_jtrans->blocklist[0];
      curr_tail->item->flags |= B_DIRTY;
      mt->bufs = curr_tail;

      // Read the data in the transaction
      uint tran_d_blk;
      // iterate over all data blocks beyond the first one in the transaction
      for (tran_d_blk = bnum+2; tran_d_blk <= bnum + mt->size; tran_d_blk++) {
	// Add an entry to the buffer list.
	struct t_data_node* tdn = (struct t_data_node*) kmalloc(sizeof(struct t_data_node));
	tdn->item = journal_blocks[tran_d_blk];
	tdn->next = 0;
	tdn->item->flags |= B_DIRTY;

	// change the sector appropriatly
	journal_blocks[tran_d_blk]->sector = curr_jtrans->blocklist[tran_d_blk-bnum - 1];
	curr_tail->next = tdn;
	curr_tail = curr_tail->next;
      }

      // Verify the checksum!
      if (curr_jtrans->checksum == 555) {
	// Add the mem_trans to the tail of mem_trans_node list.
	struct mem_trans_node* mtn = (struct mem_trans_node*) kmalloc(sizeof(struct mem_trans_node));                  
	mtn->trans = mt;   // place transaction in node
	if (mtn_tail == 0) {
	  mtn->next = 0;
	  mtn_head = mtn_tail = mtn;
	} else {
	  mtn_tail->next = mtn; // this node = new tail in jsb.
	  mtn_tail = mtn_tail->next;
	}
      } else {
	cprintf("Invalid checksum: %d on journal header block in fs %d - Skipping!", curr_jtrans->checksum, bnum+3);
        // size may be wrong when checksum fails so quit procedure.
	return;
      }
      // Increment bnum
      bnum += mt->size+1;
    }
  }
  cprintf("Doing full flush!!! Yay!!\n");
  // Flush the journal to disk!
  jfull_flush();
}

void 
end_trans() {
  struct mem_trans* ct = curmt();

  if (ct->size > 0) {

    // can't be interupted
    pushcli();

    while (xchg(&trans_write_lock.locked,1) == 1)
      ;

    // have lock now
    popcli();

    // creating header for transaction
    jt.checksum = 555;
    jt.num_data_blocks = ct->size;

    struct t_data_node* curr_buf;
    int curr_block_num = 0;
    for (curr_buf = ct->bufs; curr_buf; curr_buf = curr_buf->next) {
      jt.blocklist[curr_block_num] = curr_buf->item->sector;
      curr_block_num++;
    }

    memmove(jtrans_header_buf.data, &jt, sizeof(jt));
    jtrans_header_buf.flags |= B_BUSY;
    jtrans_header_buf.flags |= B_DIRTY;
    jbwrite(&jtrans_header_buf);
    for (curr_buf = ct->bufs; curr_buf; curr_buf = curr_buf->next) {
      jbwrite(curr_buf->item);
      curr_buf->item->flags |= B_DIRTY;
    }
    struct mem_trans_node* new_mtn = (struct mem_trans_node*) kmalloc(sizeof(struct mem_trans_node));
    new_mtn->trans = ct;
    new_mtn->next = 0;  // adding transaction to transaction list

    acquire(&jcache_lock);
    if (mtn_head == 0) {
      mtn_tail = mtn_head = new_mtn;
    } else {
      mtn_tail->next = new_mtn;  // modifying transaction list tail
    }
    release(&jcache_lock);
    
    pushcli();
    // done, return lock
    xchg(&trans_write_lock.locked, 0);
    popcli();

    if (jsb->size == 0) {
      jfull_flush();
    }
    cprintf("resetCurtrans!\n");
    resetcurtrans(); // Implement this in proc.c
  }
}


// Release the buffer buf.
void
brelse(struct buf *b)
{
  if((b->flags & B_BUSY) == 0)
    panic("brelse");

  struct mem_trans* ct = curmt();

  if (ct->size == 0 || ((b->flags & B_DIRTY) == 0)) {
    jbrelse(b);
    return;
  }
}
