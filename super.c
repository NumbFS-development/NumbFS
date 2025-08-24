// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include "disk.h"
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>

const struct super_operations numbfs_sops = {
};

static int numbfs_read_superblock(struct super_block *sb)
{
        // TODO: xxx
        return 0;
}

static int numbfs_fc_fill_super(struct super_block *sb, struct fs_context *fc)
{
        struct numbfs_superblock_info *sbi;
        struct inode *inode;
        int err;

        sb->s_magic = NUMBFS_MAGIC;
        sb->s_flags = 0;
        sb->s_maxbytes = NUMBFS_BYTES_PER_BLOCK * NUMBFS_NUM_DATA_ENTRY;
        sb->s_op = &numbfs_sops;
        sb->s_time_gran = 1;
        // TODO: xxx
        sb->s_xattr = NULL;
        // TODO: xxx
        sb->s_export_op = NULL;
        if (!sb_set_blocksize(sb, NUMBFS_BYTES_PER_BLOCK)) {
                err = -EINVAL;
                goto err_exit;
        }

        sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
        if (!sbi)
                return -ENOMEM;
        sbi->block_bits = NUMBFS_BLOCK_BITS;
        spin_lock_init(&sbi->s_lock);
        mutex_init(&sbi->s_mutex);

        sb->s_fs_info = sbi;

        err = numbfs_read_superblock(sb);
        if (err)
                goto err_exit;


        inode = numbfs_iget(sb, NUMBFS_ROOT_NID);
        if (IS_ERR(inode)) {
                err = PTR_ERR(inode);
                goto err_exit;
        }

        if (!S_ISDIR(inode->i_mode)) {
                pr_err("numbfs: root inode is not a directory, inode:%o\n",
                       inode->i_mode);
                iput(inode);
                err = -EINVAL;
                goto err_exit;
        }

        sb->s_root = d_make_root(inode);
        if (!sb->s_root) {
                err = -ENOMEM;
                goto err_exit;
        }

        pr_info("numbfs: mounted with root inode@%d\n", NUMBFS_ROOT_NID);

        return 0;
err_exit:
        sb->s_fs_info = NULL;
        kfree(sbi);
        return err;
}

static int numbfs_fc_get_tree(struct fs_context *fc)
{
        return get_tree_bdev(fc, numbfs_fc_fill_super);
}

static const struct fs_context_operations numbfs_context_ops = {
        .get_tree       = numbfs_fc_get_tree,
};

static int numbfs_init_fs_context(struct fs_context *fc)
{
        if (fc->sb_flags & SB_KERNMOUNT)
                return -EINVAL;

        fc->ops = &numbfs_context_ops;
        return 0;
}

static void numbfs_kill_sb(struct super_block *sb)
{
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);

        kill_block_super(sb);
        kfree(sbi);
        sb->s_fs_info = NULL;
}

struct file_system_type numbfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "numbfs",
	.init_fs_context = numbfs_init_fs_context,
	.kill_sb = numbfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("numbfs");

static int __init numbfs_module_init(void)
{
        return register_filesystem(&numbfs_fs_type);
}

static void __exit numbfs_module_exit(void)
{
	unregister_filesystem(&numbfs_fs_type);
}

module_init(numbfs_module_init);
module_exit(numbfs_module_exit);

MODULE_DESCRIPTION("NumbFS File System");
MODULE_AUTHOR("Hongzhen Luo");
MODULE_LICENSE("GPL");
