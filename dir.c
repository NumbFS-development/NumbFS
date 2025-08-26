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

static void numbfs_dir_init_inode(struct inode *inode, struct inode *dir,
                                  int nid, umode_t mode)
{
        struct numbfs_inode_info *ni = NUMBFS_I(inode);
        int i;

        inode->i_ino = nid;
        inode->i_mode = mode;
        inode->i_uid = current_fsuid();
        inode->i_gid = dir->i_gid;
        set_nlink(inode, S_ISDIR(mode) ? 2 : 1);
        inode->i_size = 0;
        inode->i_blocks = 0;

        if (S_ISDIR(mode))
                numbfs_dir_set_ops(inode);
        else
                numbfs_file_set_ops(inode);

        ni->sbi = NUMBFS_SB(inode->i_sb);
        ni->nid = nid;
        for (i = 0; i < NUMBFS_NUM_DATA_ENTRY; i++)
                ni->data[i] = NUMBFS_HOLE;
}

/* return a locked new inode */
static struct inode *numbfs_dir_ialloc(struct inode *dir, umode_t mode)
{
        struct inode *inode;
        int nid, err;

        err = numbfs_ialloc(dir->i_sb, &nid);
        if (err)
                return ERR_PTR(err);

        inode = new_inode(dir->i_sb);
        if (!inode)
                return ERR_PTR(-ENOMEM);

        numbfs_dir_init_inode(inode, dir, nid, mode);

        err = insert_inode_locked(inode);
        if (err < 0) {
                pr_err("numbfs: failed to insert inode, err: %d\n", err);
                return ERR_PTR(-EINVAL);
        }

        mark_inode_dirty(inode);

        return inode;
}

/* position == 0: append a dirent */
static int numbfs_write_dir(struct inode *dir, umode_t mode, const char *name,
                            int namelen, int nid, int position)
{
        struct folio *folio;
        struct numbfs_dirent *de;
        int size, off;
        void *kaddr;

        if (position)
                size = position;
        else
                size = i_size_read(dir);
        folio = read_cache_folio(dir->i_mapping, size >> PAGE_SHIFT,
                                 NULL, NULL);
        if (IS_ERR(folio))
                return PTR_ERR(folio);

        /* append a dirent in dir's address space */
        folio_lock(folio);
        kaddr = kmap_local_folio(folio, 0);
        off = size & (folio_size(folio) - 1);
        de = (struct numbfs_dirent*)((unsigned char*)kaddr + off);
        de->ino = cpu_to_le16(nid);
        memcpy(de->name, name, namelen);
        de->name_len = namelen;
        de->type = fs_umode_to_dtype(mode);

        iomap_dirty_folio(dir->i_mapping, folio);
        folio_unlock(folio);
        folio_release_kmap(folio, kaddr);

        /* update metadata */
        if (!position) {
                numbfs_setsize(dir, size + sizeof(*de));
                mark_inode_dirty(dir);
        }

	return filemap_write_and_wait(dir->i_mapping);
}

static int numbfs_dir_create(struct mnt_idmap *idmap, struct inode *dir,
                             struct dentry *dentry, umode_t mode, bool excl)
{
        struct inode *inode;
        const char *name = dentry->d_name.name;
        int namelen = dentry->d_name.len;
        int nid;

        if (numbfs_inode_by_name(dir, dentry->d_name.name,
                        dentry->d_name.len,&nid, NULL) != -ENOENT)
                return -EEXIST;

        /* alloc a initialized inode */
        inode = numbfs_dir_ialloc(dir, mode);
        if (IS_ERR(inode))
                return PTR_ERR(inode);

        /* instantiate inode and entry */
        d_instantiate_new(dentry, inode);

        /* append a new dirent */
        return numbfs_write_dir(dir, mode, name, namelen, inode->i_ino, 0);
}


const struct inode_operations numbfs_dir_iops = {
        .lookup         = numbfs_dir_lookup,
        .create         = numbfs_dir_create,
};

const struct file_operations numbfs_dir_fops = {
        .llseek         = generic_file_llseek,
        .read           = generic_read_dir,
        .iterate_shared = numbfs_readdir,
};