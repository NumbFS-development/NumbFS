// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>

void numbfs_init_buf(struct numbfs_buf *buf, struct inode *inode,
                         int blk)
{
        buf->inode = inode;
        buf->blkaddr = blk;
        buf->folio = NULL;
        buf->base = NULL;
}

int numbfs_read_buf(struct numbfs_buf *buf)
{
        struct inode *inode = buf->inode;
        pgoff_t index = (buf->blkaddr << NUMBFS_BLOCK_BITS) >> PAGE_SHIFT;
        struct folio *folio;

        folio = read_cache_folio(inode->i_mapping, index, NULL, NULL);
        if (IS_ERR(folio)) {
                pr_info("numbfs: folio is error in numbfs_read_buf\n");
                return PTR_ERR(folio);
        }

        buf->base = kmap_local_folio(folio, 0);
        buf->folio = folio;
        return 0;
}

int numbfs_commit_buf(struct numbfs_buf *buf)
{
        struct inode *inode = buf->inode;
        struct folio *folio = buf->folio;

        folio_mark_dirty(folio);
        block_write_end(NULL, inode->i_mapping, folio_pos(folio),
                folio_size(folio), folio_size(folio), &folio->page, NULL);
        return 0;
}

void numbfs_put_buf(struct numbfs_buf *buf)
{
        if (!buf->folio)
                return;

        kunmap_local(buf->base);
        buf->base = NULL;
        folio_put(buf->folio);
        buf->folio = NULL;
}

/* @addr is the address of a page, caller should put the buf */
struct numbfs_inode *numbfs_idisk(struct numbfs_buf *buf,
                                  struct super_block *sb, int nid)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        struct numbfs_inode *ret;
        int err;

        err = numbfs_binit(buf, sb->s_bdev, numbfs_inode_blk(sbi, nid));
        if (err)
                return ERR_PTR(err);

        err = numbfs_brw(buf, NUMBFS_READ);
        if (err)
                return ERR_PTR(err);

        ret = ((struct numbfs_inode*)buf->base) +
                        (nid % NUMBFS_NODES_PER_BLOCK);
        return ret;
}

/**
 * numbfs_iaddrspace_blkaddr - Get or allocate a block address for a given file position
 * @ni: Pointer to the numbfs inode info structure
 * @pos: File position (byte offset) to map to a block
 * @alloc: If true, allocate a new block if the position is a hole
 *
 * Return:
 *   On success, returns the physical block number (>= 0).
 *   If @pos is out of range, returns -E2BIG.
 *   If block allocation fails (when @alloc is true), returns the error code
 *   from numbfs_balloc().
 */
int numbfs_iaddrspace_blkaddr(struct numbfs_inode_info *ni,
                              unsigned long pos, bool alloc)
{
        int blk, err;

        if ((pos / NUMBFS_BYTES_PER_BLOCK) >= NUMBFS_NUM_DATA_ENTRY) {
                pr_err("numbfs: pos@%ld is out of range\n", pos);
                return -E2BIG;
        }

        blk = ni->data[pos / NUMBFS_BYTES_PER_BLOCK];
        if (alloc && blk == NUMBFS_HOLE) {
                err = numbfs_balloc(ni->vfs_inode.i_sb, &blk);
                if (err)
                        return err;
                ni->data[pos / NUMBFS_BYTES_PER_BLOCK] = blk;
        }
        return blk;
}

static int numbfs_bitmap_alloc(struct super_block *sb, int startblk,
                               int total, int *res, int *quota)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int err, i, byte, bit;
        struct numbfs_buf buf;
        unsigned char *bitmap;

        err = -ENOMEM;
        *res = -1;
        mutex_lock(&sbi->s_mutex);
        /* run out of quota */
        if (!*quota)
                goto out;
        for (i = 0; i < total; i++) {
                if (i % NUMBFS_BLOCKS_PER_BLOCK == 0) {
                        if (i > 0) {
                                numbfs_bput(&buf);
                        }

                        err = numbfs_binit(&buf, sb->s_bdev,
                                           numbfs_bmap_blk(startblk, i));
                        if (err) {
                                pr_err("numbfs: failed to init buffer\n");
                                goto out;
                        }

                        err = numbfs_brw(&buf, NUMBFS_READ);
                        if (err) {
                                pr_err("numbfs: failed to read bitmap block@%d\n", buf.blkaddr);
                                goto out;
                        }

                        bitmap = (unsigned char*)buf.base;
                }


                byte = numbfs_bmap_byte(i);
                bit = numbfs_bmap_bit(i);
                if (!(bitmap[byte] & (1 << bit))) {
                        *res = i;
                        bitmap[byte] |= (1 << bit);
                        err = numbfs_brw(&buf, NUMBFS_WRITE);
                        break;
                }

        }
        if (!err)
                *quota -= 1;;
out:
        mutex_unlock(&sbi->s_mutex);
        numbfs_bput(&buf);
        return err;
}

static int numbfs_bitmap_free(struct super_block *sb, int startblk, int free,
                              int *quota)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int err, byte, bit;
        struct numbfs_buf buf;
        unsigned char *bitmap;

        mutex_lock(&sbi->s_mutex);
        err = numbfs_binit(&buf, sb->s_bdev, numbfs_bmap_blk(startblk, free));
        if (err)
                goto out;

        err = numbfs_brw(&buf, NUMBFS_READ);
        if (err)
                goto out;

        bitmap = (unsigned char*)buf.base;
        byte = numbfs_bmap_byte(free);
        bit = numbfs_bmap_bit(free);
        WARN_ON(!(bitmap[byte] & (1 << bit)));
        /* mark this folio dirty */
        bitmap[byte] &= ~(1 << bit);
        err = numbfs_brw(&buf, NUMBFS_WRITE);
        if (err)
                goto out;
        *quota += 1;
out:
        mutex_unlock(&sbi->s_mutex);
        numbfs_bput(&buf);
        return err;
}

int numbfs_balloc(struct super_block *sb, int *blk)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int res, err;

        err = numbfs_bitmap_alloc(sb, sbi->bbitmap_start, sbi->data_blocks,
                                  &res, &sbi->free_blocks);
        if (err)
                return err;

        *blk = res;
        return 0;
}

int numbfs_bfree(struct super_block *sb, int blk)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);

        if (blk >= sbi->data_blocks)
                return -EINVAL;

        return numbfs_bitmap_free(sb, sbi->bbitmap_start, blk,
                                  &sbi->free_blocks);
}

int numbfs_ialloc(struct super_block *sb, int *nid)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int res, err;

        err = numbfs_bitmap_alloc(sb, sbi->ibitmap_start, sbi->total_inodes,
                                  &res, &sbi->free_inodes);
        if (err)
                return err;

        *nid = res;
        return 0;
}

int numbfs_ifree(struct super_block *sb, int nid)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);

        if (nid >= sbi->total_inodes)
                return -EINVAL;

        return numbfs_bitmap_free(sb, sbi->ibitmap_start, nid,
                                  &sbi->free_inodes);
}
