// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <uapi/asm-generic/errno-base.h>
#include <linux/namei.h>

static int numbfs_fill_inode(struct inode *inode)
{
        //TODO: XXX
        inode->i_mode = (inode->i_mode & ~S_IFMT) | S_IFDIR;
        return 0;
}

static int numbfs_iget5_eq(struct inode *inode, void *nid)
{
        // TODO: XXX
        return 0;
}

static int numbfs_iget5_set(struct inode *inode, void *data)
{
        // TODO: XXX
        return 0;
}


struct inode *numbfs_iget(struct super_block *sb, int nid)
{
        struct inode *inode;
        int err;

        inode = iget5_locked(sb, nid, numbfs_iget5_eq,
                             numbfs_iget5_set, &nid);
        if (!inode)
                return ERR_PTR(-ENOMEM);

        if (inode->i_state & I_NEW) {
                err = numbfs_fill_inode(inode);
                if (err) {
                        iget_failed(inode);
                        return ERR_PTR(err);
                }
                unlock_new_inode(inode);

        }
        return inode;
}
