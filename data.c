// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/iomap.h>

#define NUMBFS_READ     0
#define NUMBFS_WRITE    1

static int numbfs_iomap(struct inode *inode, loff_t offset, loff_t length,
                        struct iomap *iomap, int type)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        int blk;

        blk = numbfs_iaddrspace_blkaddr(ni, offset, type == NUMBFS_WRITE);
        if (blk < 0 && blk != NUMBFS_HOLE)
                return -EINVAL;

        iomap->flags = 0;
        iomap->offset = (offset >> NUMBFS_BLOCK_BITS) << NUMBFS_BLOCK_BITS;
        iomap->bdev = inode->i_sb->s_bdev;
        iomap->length = 1 << NUMBFS_BLOCK_BITS;
        iomap->private = NULL;

        if (blk == NUMBFS_HOLE) {
                iomap->type = IOMAP_HOLE;
                iomap->addr = IOMAP_NULL_ADDR;
                iomap->flags |= IOMAP_F_NEW;
                return 0;
        }

        iomap->type = IOMAP_MAPPED;
        iomap->addr = numbfs_data_blk(ni->sbi, blk) << NUMBFS_BLOCK_BITS;
        return 0;
}

static int numbfs_iomap_read_begin(struct inode *inode, loff_t offset,
                loff_t length, unsigned int flags, struct iomap *iomap,
                struct iomap *srcmap)
{
        return numbfs_iomap(inode, offset, length, iomap, NUMBFS_READ);
}

const struct iomap_ops numbfs_iomap_read_ops = {
        .iomap_begin    = numbfs_iomap_read_begin,
};

static int numbfs_read_folio(struct file *file, struct folio *folio)
{
        return iomap_read_folio(folio, &numbfs_iomap_read_ops);
}

static int numbfs_map_blocks(struct iomap_writepage_ctx *wpc,
                             struct inode *inode, loff_t offset)
{
        return numbfs_iomap(inode, offset, NUMBFS_BYTES_PER_BLOCK,
                            &wpc->iomap, NUMBFS_WRITE);
}

static const struct iomap_writeback_ops numbfs_writeback_ops = {
        .map_blocks = numbfs_map_blocks,
};

static int numbfs_writepages(struct address_space *mapping,
                             struct writeback_control *wbc)
{
	struct iomap_writepage_ctx ctx = {};

	return iomap_writepages(mapping, wbc, &ctx, &numbfs_writeback_ops);
}

const struct address_space_operations numbfs_aops = {
        .read_folio             = numbfs_read_folio,
        .writepages             = numbfs_writepages,
};

const struct file_operations numbfs_file_fops = {
};
