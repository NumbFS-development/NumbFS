// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <uapi/asm-generic/errno-base.h>
#include <linux/namei.h>

static int numbfs_fill_inode(struct inode *inode)
{
        struct super_block *sb = inode->i_sb;
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        struct numbfs_buf buf;
        struct numbfs_inode *di;
        int err, i;

        /* on-disk inode information */
        di = numbfs_idisk(&buf, sb, inode->i_ino);
        if (IS_ERR(di)) {
                numbfs_put_buf(&buf);
                return PTR_ERR(di);
        }

        inode->i_mode = le32_to_cpu(di->i_mode);
        i_uid_write(inode, le16_to_cpu(di->i_uid));
        i_gid_write(inode, le16_to_cpu(di->i_gid));
        set_nlink(inode, le16_to_cpu(di->i_nlink));
        inode->i_size = le32_to_cpu(di->i_size);
        inode->i_blocks = round_up(inode->i_size, sb->s_blocksize) >> 9;

        ni->sbi = NUMBFS_SB(sb);
        ni->nid = inode->i_ino;
        for (i = 0; i < NUMBFS_NUM_DATA_ENTRY; i++)
                ni->data[i] = le32_to_cpu(di->i_data[i]);

        numbfs_put_buf(&buf);

        err = 0;
        switch(inode->i_mode & S_IFMT) {
        case S_IFREG:
                err = -EOPNOTSUPP;
                break;
        case S_IFLNK:
                err = -EOPNOTSUPP;
                break;
        case S_IFDIR:
                // TODO: XXX
                err = 0;
                break;
        default:
                err = -EOPNOTSUPP;
                goto out;
        }
out:
        return err;
}

static int numbfs_iget5_eq(struct inode *inode, void *nid)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);

        return ni->nid == *(int*)nid;
}

static int numbfs_iget5_set(struct inode *inode, void *data)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        int nid = *(int*)data;

        inode->i_ino = nid;
        ni->nid = nid;
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
