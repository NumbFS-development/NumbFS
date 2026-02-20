// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

/*
 * numbfs internal structures and utilities
 *
 * This header defines in-memory structures and utilities for the NUMBFS filesystem implementation.
 * It includes:
 * - Superblock information structure (cached on-disk metadata with synchronization primitives)
 * - Buffer management structure for block I/O operations
 * - In-memory inode information structure extending VFS inode
 * - Function declarations for filesystem operations (super, inode, file, address space ops)
 * - Utility macros and functions for:
 *   * Bitmap calculations (block and inode allocation)
 *   * Block address translation
 *   * Inode operations
 *   * Buffer management
 *
 * This header bridges the on-disk structures (from disk.h) with the VFS interfaces.
 */

#ifndef __NUMBFS_INTERNEL_H
#define __NUMBFS_INTERNEL_H

#include "disk.h"
#include <linux/fs.h>
#include <linux/blk_types.h>
#include <linux/byteorder/generic.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/spinlock.h>
#include <linux/statfs.h>
#include <linux/mutex.h>
#include <linux/bio.h>

#define NUMBFS_BLOCK_BITS	9
#define NUMBFS_BLOCK_SIZE	(1 << NUMBFS_BLOCK_BITS)

struct numbfs_superblock_info {
	/* on-disk information */
	int feature;
	int total_inodes;
	int free_inodes;
	int data_blocks;
	int free_blocks;
	int ibitmap_start;
	int inode_start;
	int bbitmap_start;
	int data_start;

	int block_bits;

	spinlock_t s_lock;
	struct mutex s_mutex;
 };

struct numbfs_buf {
	/* for the address space of a inode */
	struct inode *inode;
	/* for the address space of disk */
	struct block_device *bdev;
	int blkaddr;
	void *base;
	struct folio *folio;
};

struct numbfs_inode_info {
	int nid;
	int data[NUMBFS_NUM_DATA_ENTRY];
	int xattr_start;
	short xattr_count;
	struct numbfs_superblock_info *sbi;
	struct inode vfs_inode;
};

extern struct file_system_type numbfs_fs_type;
extern const struct super_operations numbfs_sops;

/* inode operations */
extern const struct inode_operations numbfs_generic_iops;
extern const struct inode_operations numbfs_symlink_iops;
extern const struct inode_operations numbfs_dir_iops;

/* file operations */
extern const struct file_operations numbfs_file_fops;
extern const struct file_operations numbfs_dir_fops;

/* address space operations */
extern const struct address_space_operations numbfs_aops;

#define NUMBFS_SB(sb) ((struct numbfs_superblock_info*)(sb->s_fs_info))

/* inode */
#define NUMBFS_I(ptr)	container_of(ptr, struct numbfs_inode_info, vfs_inode)
struct inode *numbfs_iget(struct super_block *sb, int nid);
void numbfs_setsize(struct inode *inode, loff_t newsize);
void numbfs_file_set_ops(struct inode *inode);

/* utils */
#define NUMBFS_BITS_PER_BYTE 8
#define NUMBFS_BYTES_PER_BLOCK	512
#define NUMBFS_BLOCKS_PER_BLOCK (NUMBFS_BYTES_PER_BLOCK * NUMBFS_BITS_PER_BYTE)
#define NUMBFS_NODES_PER_BLOCK  (NUMBFS_BYTES_PER_BLOCK / sizeof(struct numbfs_inode))

/* calculate the block number of the bitmap related to @blkno */
static inline int numbfs_bmap_blk(int startblk, int blkno)
{
	return startblk + blkno / NUMBFS_BLOCKS_PER_BLOCK;
}

/* calculate the byte number in the block related to @blkno */
static inline int numbfs_bmap_byte(int blkno)
{
	return  (blkno % NUMBFS_BLOCKS_PER_BLOCK) / NUMBFS_BITS_PER_BYTE;
}

/* calculate the bit number in the byte related to @blkno */
static inline int numbfs_bmap_bit(int blkno)
{
	return (blkno % NUMBFS_BLOCKS_PER_BLOCK) % NUMBFS_BITS_PER_BYTE;
}

static inline int numbfs_inode_blk(struct numbfs_superblock_info *sbi,
				   int nid)
{
	return sbi->inode_start + nid / NUMBFS_NODES_PER_BLOCK;
}

static inline int numbfs_data_blk(struct numbfs_superblock_info *sbi,
				  int blk)
{
	return sbi->data_start + blk;
}

/* read inode data */
void numbfs_ibuf_init(struct numbfs_buf *buf, struct inode *inode, int blk);
int numbfs_ibuf_read(struct numbfs_buf *buf);
void numbfs_ibuf_put(struct numbfs_buf *buf);

/* read disk data via bio */
#define NUMBFS_READ     0
#define NUMBFS_WRITE    1

int numbfs_binit(struct numbfs_buf *buf, struct block_device *bdev,
		 int blk);
int numbfs_brw(struct numbfs_buf *buf, int rw);
void numbfs_bput(struct numbfs_buf *buf);


/* caller should put the buf */
struct numbfs_inode *numbfs_idisk(struct numbfs_buf *buf,
				  struct super_block *sb, int nid);

int numbfs_iaddrspace_blkaddr(struct numbfs_inode_info *ni,
			      unsigned long pos, bool alloc);

/* block management */
int numbfs_balloc(struct super_block *sb, int *blk);
int numbfs_bfree(struct super_block *sb, int blk);
int numbfs_ialloc(struct super_block *sb, int *nid);
int numbfs_ifree(struct super_block *sb, int nid);

/* dir.c */
void numbfs_dir_set_ops(struct inode *inode);

/* xattr.c */
extern const struct xattr_handler * const numbfs_xattr_handlers[];

#endif
