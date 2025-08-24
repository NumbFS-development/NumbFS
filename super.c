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

static struct kmem_cache *numbfs_inode_cachep __read_mostly;

static void numbfs_inode_init_once(void *ptr)
{
        struct numbfs_inode_info *ni = ptr;

        inode_init_once(&ni->vfs_inode);
}

static struct inode *numbfs_alloc_inode(struct super_block *sb)
{
        struct numbfs_inode_info *ni =
                        alloc_inode_sb(sb, numbfs_inode_cachep, GFP_KERNEL);

        if (!ni)
                return NULL;

        /* set everything except vfs_inode to zero */
        memset(ni, 0, offsetof(struct numbfs_inode_info, vfs_inode));
        return &ni->vfs_inode;
}

static void numbfs_free_inode(struct inode *inode)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);

        kmem_cache_free(numbfs_inode_cachep, ni);
}

static void numbfs_put_super(struct super_block *sb)
{
        struct numbfs_buf buf;
        struct numbfs_super_block *nsb;
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int offset = NUMBFS_SUPER_OFFSET & (PAGE_SIZE - 1);
        int err = 0;

        numbfs_init_buf(&buf, sb->s_bdev->bd_inode, NUMBFS_SUPER_OFFSET >> NUMBFS_BLOCK_BITS);
        err = numbfs_read_buf(&buf);
        if (err) {
                pr_err("numbfs: failed to read superblock\n");
                goto exit;
        }

        err = -EINVAL;
        nsb = (struct numbfs_super_block*)(buf.base + offset);
        if (le32_to_cpu(nsb->s_magic) != NUMBFS_MAGIC) {
                pr_err("numbfs: can not find a valid superblock\n");
                goto exit;
        }

        nsb->s_feature          = cpu_to_le32(sbi->feature);
        nsb->s_total_inodes     = cpu_to_le32(sbi->total_inodes);
        nsb->s_free_inodes      = cpu_to_le32(sbi->free_inodes);
        nsb->s_data_blocks      = cpu_to_le32(sbi->data_blocks);
        nsb->s_free_blocks      = cpu_to_le32(sbi->free_blocks);
        nsb->s_ibitmap_start    = cpu_to_le32(sbi->ibitmap_start);
        nsb->s_inode_start      = cpu_to_le32(sbi->inode_start);
        nsb->s_bbitmap_start    = cpu_to_le32(sbi->bbitmap_start);
        nsb->s_data_start       = cpu_to_le32(sbi->data_start);

        err = numbfs_commit_buf(&buf);
        if (err)
                pr_err("numbfs: failded to write superblock to disk.\n");
exit:
        numbfs_put_buf(&buf);
}

static void numbfs_dump_inode(struct inode *inode, struct numbfs_inode *di)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        int i;

        di->i_ino = cpu_to_le16(inode->i_ino);
        di->i_mode = cpu_to_le32(inode->i_mode);
        di->i_nlink = cpu_to_le16(inode->i_nlink);
        di->i_uid = cpu_to_le16(__kuid_val(inode->i_uid));
        di->i_gid = cpu_to_le16(__kgid_val(inode->i_gid));
        di->i_size = cpu_to_le32(inode->i_size);
        for (i = 0; i < NUMBFS_NUM_DATA_ENTRY; i++)
                di->i_data[i] = cpu_to_le32(ni->data[i]);
}

static int numbfs_write_inode_meta(struct inode *inode)
{
        struct numbfs_buf buf;
        struct numbfs_inode *di;
        int nid = inode->i_ino;
        int err;

        di = numbfs_idisk(&buf, inode->i_sb, nid);
        if (IS_ERR(di)) {
                err = PTR_ERR(di);
                goto out;
        }

        folio_lock(buf.folio);
        numbfs_dump_inode(inode, di);
        err = numbfs_commit_buf(&buf);
        folio_unlock(buf.folio);
out:
        numbfs_put_buf(&buf);
        return err;
}

static int numbfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
        return numbfs_write_inode_meta(inode);
}

const struct super_operations numbfs_sops = {
        .alloc_inode    = numbfs_alloc_inode,
        .free_inode     = numbfs_free_inode,
        .write_inode    = numbfs_write_inode,
        .put_super      = numbfs_put_super,
};

static int numbfs_read_superblock(struct super_block *sb)
{
        struct numbfs_buf buf;
        struct numbfs_super_block *nsb;
        struct numbfs_superblock_info *sbi = NUMBFS_SB(sb);
        int offset = NUMBFS_SUPER_OFFSET & (PAGE_SIZE - 1);
        int err = 0;

        numbfs_init_buf(&buf, sb->s_bdev->bd_inode, NUMBFS_SUPER_OFFSET >> NUMBFS_BLOCK_BITS);

        err = numbfs_read_buf(&buf);
        if (err) {
                pr_err("numbfs: failed to read superblock\n");
                goto exit;
        }

        err = -EINVAL;
        nsb = (struct numbfs_super_block*)(buf.base + offset);
        if (le32_to_cpu(nsb->s_magic) != NUMBFS_MAGIC) {
                pr_err("numbfs: can not find a valid superblock\n");
                goto exit;
        }

        sbi->feature = le32_to_cpu(nsb->s_feature);
        sbi->total_inodes = le32_to_cpu(nsb->s_total_inodes);
        sbi->free_inodes = le32_to_cpu(nsb->s_free_inodes);
        sbi->data_blocks = le32_to_cpu(nsb->s_data_blocks);
        sbi->free_blocks = le32_to_cpu(nsb->s_free_blocks);
        sbi->ibitmap_start = le32_to_cpu(nsb->s_ibitmap_start);
        sbi->inode_start = le32_to_cpu(nsb->s_inode_start);
        sbi->bbitmap_start = le32_to_cpu(nsb->s_bbitmap_start);
        sbi->data_start = le32_to_cpu(nsb->s_data_start);
        sbi->block_bits = NUMBFS_BLOCK_BITS;

        err = 0;
exit:
        numbfs_put_buf(&buf);
        return err;
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
        numbfs_inode_cachep = kmem_cache_create("numbfs_inodes",
                        sizeof(struct numbfs_inode_info), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT,
			numbfs_inode_init_once);
        if (!numbfs_inode_cachep)
                return -ENOMEM;

        return register_filesystem(&numbfs_fs_type);
}

static void __exit numbfs_module_exit(void)
{
        kmem_cache_destroy(numbfs_inode_cachep);
	unregister_filesystem(&numbfs_fs_type);
}

module_init(numbfs_module_init);
module_exit(numbfs_module_exit);

MODULE_DESCRIPTION("NumbFS File System");
MODULE_AUTHOR("Hongzhen Luo");
MODULE_LICENSE("GPL");
