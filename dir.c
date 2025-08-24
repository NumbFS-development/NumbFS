// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <linux/pagemap.h>
#include <linux/iomap.h>

void numbfs_dir_set_ops(struct inode *inode)
{
        inode->i_op = &numbfs_dir_iops;
        inode->i_fop = &numbfs_dir_fops;
        inode->i_mapping->a_ops = &numbfs_aops;
}

/* find the target nid according to the name */
static int numbfs_inode_by_name(struct inode *dir, const char *name,
                                int namelen, int *nid, int *offset)
{
        struct numbfs_dirent *de;
        struct numbfs_buf buf;
        int i, ret, err;

        ret = -ENOENT;
        for (i = 0; i < dir->i_size; i += sizeof(*de)) {
                if (i % NUMBFS_BYTES_PER_BLOCK == 0) {
                        if (i > 0)
                                numbfs_put_buf(&buf);

                        numbfs_init_buf(&buf, dir, i / NUMBFS_BYTES_PER_BLOCK);
                        err = numbfs_read_buf(&buf);
                        if (err)
                                return err;
                }
                de = (struct numbfs_dirent*)((unsigned char*)buf.base +
                                (i % NUMBFS_BYTES_PER_BLOCK));
                if (de->name_len == namelen &&
                    !memcmp(name, de->name, namelen)) {
                        ret = 0;
                        *nid = le16_to_cpu(de->ino);
                        if (offset)
                                *offset = i;
                        break;
                }

        }

        numbfs_put_buf(&buf);
        return ret;
}

static int numbfs_readdir(struct file *file, struct dir_context *ctx)
{
        struct inode *dir = file_inode(file);
        size_t dirsize = i_size_read(dir);
        struct numbfs_buf buf;
        struct numbfs_dirent *de;
        int err;

        while (ctx->pos < dirsize) {
                const char *de_name;
                unsigned int de_namelen;
                unsigned char de_type;

                if (ctx->pos % NUMBFS_BYTES_PER_BLOCK == 0) {
                        if (ctx->pos > 0)
                                numbfs_put_buf(&buf);
                        numbfs_init_buf(&buf, dir, ctx->pos / NUMBFS_BYTES_PER_BLOCK);
                        err = numbfs_read_buf(&buf);
                        if (err) {
                                pr_info("numbfs: error to read dir block@%lld, err: %d\n", ctx->pos / NUMBFS_BYTES_PER_BLOCK, err);
                                goto out;
                        }

                }

                de = (struct numbfs_dirent*)((unsigned char*)buf.base +
                                (ctx->pos % NUMBFS_BYTES_PER_BLOCK));
                de_name = de->name;
                de_namelen = de->name_len;
                de_type = de->type;
                ctx->pos += sizeof(struct numbfs_dirent);
                if (!de_namelen) {
                        pr_err("numbfs: invalid dirent: namelen=0\n");
                        err = -EINVAL;
                        goto out;
                }

                if (!dir_emit(ctx, de_name, de_namelen, le16_to_cpu(de->ino),
                              de_type)) {
			err = 0;
			goto out;
                }
        }
out:
        numbfs_put_buf(&buf);
        return err;
}

static struct dentry *numbfs_dir_lookup(struct inode *dir,
                struct dentry *dentry, unsigned int flags)
{
        int res, ino;
        struct inode *inode;

        if (dentry->d_name.len > NUMBFS_MAX_PATH_LEN)
                return ERR_PTR(-ENAMETOOLONG);

        res = numbfs_inode_by_name(dir, dentry->d_name.name,
                                dentry->d_name.len, &ino, NULL);
        if (res) {
                if (res != -ENOENT)
                        return ERR_PTR(res);
                inode = NULL;
        } else {
                inode = numbfs_iget(dir->i_sb, ino);
                if (IS_ERR(inode))
                        return ERR_PTR(PTR_ERR(inode));
        }

        return d_splice_alias(inode, dentry);
}

const struct inode_operations numbfs_dir_iops = {
        .lookup         = numbfs_dir_lookup,
};

const struct file_operations numbfs_dir_fops = {
        .llseek         = generic_file_llseek,
        .read           = generic_read_dir,
        .iterate_shared = numbfs_readdir,
};