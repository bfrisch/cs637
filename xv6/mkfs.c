#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "types.h"
#include "fs.h"
#include "pbj.h"
#include "param.h"

int nblocks = -1; // Number of free blocks?
int ninodes = 200;
int size = -1;
int jblocks = -1;

int fsfd;
struct superblock sb;
char zeroes[512];
uint freeblock;
uint usedblocks;
uint bitblocks;
uint freeinode = 1;

void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void jinit(void);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*) &y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*) &y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd, kb_fs_size;
  uint rootino, inum, off;
  struct dirent de;
  char buf[512];
  struct dinode din;

  if(argc < 3 || (argc > 2 && (kb_fs_size = atoi(argv[1])) < 1)) {
    fprintf(stderr, "Usage: mkfs fs_size fs.img files...\n");
    exit(1);
  }

  assert((512 % sizeof(struct dinode)) == 0);
  assert((512 % sizeof(struct dirent)) == 0);
  assert(kb_fs_size <= 32767); // Limit is 32 MB FS Size

  fsfd = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[2]);
    exit(1);
  }

  size = (kb_fs_size * 1024) / BSIZE; // Total blocks in FS
  jblocks = NJB; // Total blocks used for journal
  bitblocks = size/(512*8) + 1; // Number of blocks used for bitmap.
  usedblocks = (ninodes / IPB) + 3 + bitblocks + jblocks; // Number of blocks in use right now
  freeblock = usedblocks; // Address of first free block.
  nblocks = size - usedblocks; // Total free blocks available

  sb.size = xint(size);
  sb.nblocks = xint(nblocks); // so whole disk is size sectors
  sb.ninodes = xint(ninodes);

  printf("used %d (bit %d ninode %lu journal %d) free %u total %d\n", usedblocks,
         bitblocks, ninodes/IPB + 1, jblocks, freeblock, nblocks+usedblocks);

  assert(nblocks + usedblocks == size);

  for(i = 0; i < nblocks + usedblocks; i++)
    wsect(i, zeroes);

  wsect(1, &sb);
  jinit();
  //jtest();

  rootino = ialloc(T_DIR);
  assert(rootino == 1);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 3; i < argc; i++){
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }
    
    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(usedblocks);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, 512) != 512){
    perror("write");
    exit(1);
  }
}

uint
i2b(uint inum)
{
  return (inum / IPB) + 2 + jblocks;
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*) buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*) buf) + (inum % IPB);
  *ip = *dip;
}

void
jinit () {
  // Write out uber-block
  struct jsblock jsb;
  jsb.size = xint(jblocks - 1);
  jsb.head_block = xint(3);
  jsb.tail_block = xint(3);
  jsb.time_stamp = xint(0);
  wsect(2, &jsb);
}

void
jtest() {
  // Write out uber-block
  struct jsblock jsb;
  jsb.size = xint(jblocks - 1);
  jsb.head_block = xint(3);
  jsb.tail_block = xint(8);
  jsb.time_stamp = xint(0);
  wsect(2, &jsb);

  int i;
  // Write out our test entry header
  struct jtrans trans;
  trans.checksum = xint(555);
  trans.num_data_blocks = xint(5);
  trans.blocklist[0] = xint(freeblock+1);
  trans.blocklist[1] = xint(freeblock+2);
  trans.blocklist[2] = xint(freeblock+3);
  trans.blocklist[3] = xint(freeblock+4);
  trans.blocklist[4] = xint(freeblock+5);
  wsect(3, &trans);

  char block[512];
  for (i = 0; i < 512; i+=4) {
    block[i] = 'B';
    block[i + 1] = 'P';
    block[i + 2] = 'J';
    block[i + 3] = '1';
  }
  wsect(4, &block);
  wsect(5, &block);
  wsect(6, &block);
  wsect(7, &block);
  wsect(8, &block);
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, 512) != 512){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[512];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  //assert(used < 512);
  bzero(buf, 512);
  for(i = 0; i < used; i++) {
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %lu\n", ninodes/IPB + jblocks + 3);
  wsect(ninodes / IPB + jblocks +  3, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*) xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[512];
  uint indirect[BSIZE / sizeof(uint)];
  uint x;

  rinode(inum, &din);

  off = xint(din.size);
  while(n > 0){
    fbn = off / 512;
  
  assert(fbn < MAXFILE);
    if(fbn < NDIRECT) {
      if(xint(din.addrs[fbn]) == 0) {
        din.addrs[fbn] = xint(freeblock++);
        usedblocks++;
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0) {
        //printf("allocate first indirect block\n");
        din.addrs[NDIRECT] = xint(freeblock++);
        usedblocks++;
      }
      //printf("read indirect block\n");
      rsect(xint(din.addrs[NDIRECT]), (char*) indirect);
      int indir_num = (fbn - NDIRECT) / (BSIZE/sizeof(uint));
      if(indirect[indir_num] == 0) {
        indirect[indir_num] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[NDIRECT]), (char*) indirect);
      }
      int sect_to_read = indirect[indir_num];
      rsect(xint(sect_to_read), (char*) indirect);
      indir_num = (fbn - NDIRECT) % (BSIZE/sizeof(uint));
      if (indirect[indir_num] == 0) {
	//printf("Alloc second indirect block!\n");
	indirect[indir_num] = xint(freeblock++);
	usedblocks++;
	wsect(xint(sect_to_read), (char*) indirect);
      }
      x = xint(indirect[indir_num]);
    }
    n1 = min(n, (fbn + 1) * 512 - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * 512), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
