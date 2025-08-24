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
