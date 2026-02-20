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

static int numbfs_set_timestamps(struct inode *inode)
{
	struct numbfs_inode_info *ni = NUMBFS_I(inode);
	struct numbfs_timestamps *nt;
	struct numbfs_buf buf;
	int err;

	err = numbfs_binit(&buf, inode->i_sb->s_bdev,
			   numbfs_data_blk(ni->sbi, ni->xattr_start));
	if (err)
		return err;

	err = numbfs_brw(&buf, NUMBFS_READ);
	if (err) {
		numbfs_bput(&buf);
		return err;
	}

	nt = (struct numbfs_timestamps*)buf.base;
	(void)inode_set_atime(inode, (time64_t)le64_to_cpu(nt->t_atime), 0);
	(void)inode_set_mtime(inode, (time64_t)le64_to_cpu(nt->t_mtime), 0);
	(void)inode_set_ctime(inode, (time64_t)le64_to_cpu(nt->t_ctime), 0);

	numbfs_bput(&buf);
	return 0;
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
		numbfs_ibuf_put(&buf);
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
	ni->xattr_start = le32_to_cpu(di->i_xattr_start);
	ni->xattr_count = di->i_xattr_count;
	numbfs_ibuf_put(&buf);

	err = numbfs_set_timestamps(inode);
	if (err)
		return err;

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

static int numbfs_getattr(struct mnt_idmap *idmap, const struct path *path,
			  struct kstat *stat, u32 request_mask,
			  unsigned int query_flags)
{
	struct inode *const inode = d_inode(path->dentry);

	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

static int numbfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			  struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int err;

	err = setattr_prepare(&nop_mnt_idmap, dentry, iattr);
	if (err)
		return err;

	if (iattr->ia_valid & ATTR_SIZE && iattr->ia_size != inode->i_size)
		numbfs_setsize(inode, iattr->ia_size);

	setattr_copy(&nop_mnt_idmap, inode, iattr);
	mark_inode_dirty(inode);

	return err;
}


const struct inode_operations numbfs_generic_iops = {
	.getattr	= numbfs_getattr,
	.setattr	= numbfs_setattr,
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

	numbfs_ibuf_init(&buf, inode, 0);
	err = numbfs_ibuf_read(&buf);
	if (err) {
		numbfs_ibuf_put(&buf);
		return ERR_PTR(err);
	}

	memcpy(target, buf.base, NUMBFS_BYTES_PER_BLOCK);
	numbfs_ibuf_put(&buf);
	nd_terminate_link(target, inode->i_size, NUMBFS_BYTES_PER_BLOCK-1);
	set_delayed_call(callback, numbfs_link_free, target);
	return target;
}

const struct inode_operations numbfs_symlink_iops = {
	.get_link	= numbfs_get_link,
	.getattr	= numbfs_getattr,
	.setattr	= numbfs_setattr,
};
