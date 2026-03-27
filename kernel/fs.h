// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 11  // 直接块数量
#define NINDIRECT (BSIZE / sizeof(uint))  // 一级间接块数量
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT) // 最大文件大小，直接块数量 + 一级间接块数量 + 二级间接块数量

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)  针对设备文件，表示设备类型
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses
};

// Inodes per block.  每块包含的inode数量
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i  计算inode i所在的块号，得到 blockno (磁盘块号)
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block   
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b   
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
// 目录是一个包含dirent结构序列的文件。
#define DIRSIZ 14

struct dirent {
  ushort inum; // inode号
  char name[DIRSIZ];// 文件名
};

