// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <uapi/asm-generic/errno-base.h>
#include <linux/namei.h>

void numbfs_file_set_ops(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode)) {
		inode->i_op             = &numbfs_symlink_iops;
		inode->i_fop            = &numbfs_file_fops;
		inode->i_mapping->a_ops = &numbfs_aops;
	} else {
		inode->i_op             = &numbfs_generic_iops;
		inode->i_fop            = &numbfs_file_fops;
		inode->i_mapping->a_ops = &numbfs_aops;
	}
}

static void numbfs_truncate_blocks(struct inode *inode, loff_t newsize)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        loff_t i = DIV_ROUND_UP(newsize, NUMBFS_BYTES_PER_BLOCK);

        for (; i < NUMBFS_NUM_DATA_ENTRY; i++)
                if (ni->data[i] != NUMBFS_HOLE)
                        numbfs_bfree(inode->i_sb, ni->data[i]);
}

void numbfs_setsize(struct inode *inode, loff_t newsize)
{
	filemap_invalidate_lock(inode->i_mapping);
	truncate_setsize(inode, newsize);
	numbfs_truncate_blocks(inode, newsize);
	filemap_invalidate_unlock(inode->i_mapping);
}

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

        i_uid_write(inode, le16_to_cpu(di->i_uid));
        i_gid_write(inode, le16_to_cpu(di->i_gid));
        set_nlink(inode, le16_to_cpu(di->i_nlink));
        inode->i_mode   = le32_to_cpu(di->i_mode);
        inode->i_size   = le32_to_cpu(di->i_size);
        inode->i_blocks = round_up(inode->i_size, sb->s_blocksize) >> 9;

        ni->sbi = NUMBFS_SB(sb);
        ni->nid = inode->i_ino;
        for (i = 0; i < NUMBFS_NUM_DATA_ENTRY; i++)
                ni->data[i] = le32_to_cpu(di->i_data[i]);

        numbfs_put_buf(&buf);

        err = 0;
        switch(inode->i_mode & S_IFMT) {
        case S_IFREG:
        case S_IFLNK:
                numbfs_file_set_ops(inode);
                err = 0;
                break;
        case S_IFDIR:
                numbfs_dir_set_ops(inode);
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

const struct inode_operations numbfs_generic_iops = {
};

static void numbfs_link_free(void *target)
{
        kfree(target);
}

static const char *numbfs_get_link(struct dentry *dentry, struct inode *inode,
			           struct delayed_call *callback)
{
        struct numbfs_buf buf;
        char *target;
        int err;

        target = kmalloc(NUMBFS_BYTES_PER_BLOCK, GFP_KERNEL);
        if (!target)
                return ERR_PTR(-ENOMEM);

        numbfs_init_buf(&buf, inode, 0);
        err = numbfs_read_buf(&buf);
        if (err) {
                numbfs_put_buf(&buf);
                return ERR_PTR(err);
        }

        memcpy(target, buf.base, NUMBFS_BYTES_PER_BLOCK);
        numbfs_put_buf(&buf);
        nd_terminate_link(target, inode->i_size, NUMBFS_BYTES_PER_BLOCK-1);
        set_delayed_call(callback, numbfs_link_free, target);
        return target;
}

const struct inode_operations numbfs_symlink_iops = {
        .get_link       = numbfs_get_link
};
