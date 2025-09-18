// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <linux/xattr.h>

static int numbfs_getxattr(struct inode *inode, int index, const char *name,
			  void *buffer, size_t buffer_size, int *offset)
{
	struct numbfs_inode_info *ni = NUMBFS_I(inode);
	struct numbfs_superblock_info *sbi = NUMBFS_SB(inode->i_sb);
	struct numbfs_buf buf;
	struct numbfs_xattr_entry *xe;
	int err, i;

	err = numbfs_binit(&buf, inode->i_sb->s_bdev,
			   numbfs_data_blk(sbi, ni->xattr_start));
	if (err)
		return err;

	err = numbfs_brw(&buf, NUMBFS_READ);
	if (err)
		goto out;

	xe = (struct numbfs_xattr_entry*)(buf.base + NUMBFS_XATTR_ENTRY_START);
	err = -ENODATA;
	for (i = 0; i < NUMBFS_XATTR_MAX_ENTRY; i++, xe++) {
		if (!xe->e_valid || xe->e_type != index ||
		    strlen(name) != xe->e_nlen)
			continue;
		if (memcmp(name, xe->e_name, xe->e_nlen))
			continue;

		/* buffer == NULL or buffer_size == 0 means that we want the length */
		if (!buffer || !buffer_size) {
			err = xe->e_vlen;
			goto out;
		}

		if (buffer_size < xe->e_vlen) {
			err = -ERANGE;
			goto out;
		}

		memcpy(buffer, xe->e_value, xe->e_vlen);
		err = xe->e_vlen;
		if (offset)
			*offset = (void*)xe - buf.base;
		break;
	}
out:
	numbfs_bput(&buf);
	return err;

}

static int numbfs_do_setxattr(struct inode *inode, int index, const char *name,
			      const void *buffer, size_t buffer_size, int offset)
{
	struct numbfs_inode_info *ni = NUMBFS_I(inode);
	struct numbfs_superblock_info *sbi = NUMBFS_SB(inode->i_sb);
	struct numbfs_xattr_entry *xe;
	struct numbfs_buf buf;
	int i, err;

	err = numbfs_binit(&buf, inode->i_sb->s_bdev,
			   numbfs_data_blk(sbi, ni->xattr_start));
	if (err)
		return err;

	err = numbfs_brw(&buf, NUMBFS_READ);
	if (err)
		goto out;

	xe = (struct numbfs_xattr_entry*)(buf.base + NUMBFS_XATTR_ENTRY_START);

	/* let's remove the xattr when buffer_size is 0 */
	if (!buffer || !buffer_size) {
		err = -ENODATA;
		for (i = 0; i < NUMBFS_XATTR_MAX_ENTRY; i++, xe++) {
			if (!xe->e_valid || xe->e_type != index ||
			    strlen(name) != xe->e_nlen)
				continue;
			if (memcmp(name, xe->e_name, xe->e_nlen))
				continue;
			xe->e_valid = 0;
			ni->xattr_count -= 1;
			err = numbfs_brw(&buf, NUMBFS_WRITE);
			break;
		}
		goto out;
	}

	/* overwrite an existing entry */
	if (offset != -1) {
		xe = (struct numbfs_xattr_entry*)(buf.base + offset);
		xe->e_valid = true;
		xe->e_type = index;
		xe->e_nlen = strlen(name);
		memcpy(xe->e_name, name, xe->e_nlen);
		xe->e_vlen = buffer_size;
		memcpy(xe->e_value, buffer, xe->e_vlen);
		err = numbfs_brw(&buf, NUMBFS_WRITE);
		goto out;
	}

	err = -ENOMEM;
	/* create a new xattr */
	for (i = 0; i < NUMBFS_XATTR_MAX_ENTRY; i++, xe++) {
		if (xe->e_valid)
			continue;

		if (strlen(name) > NUMBFS_XATTR_MAXNAME ||
		    buffer_size > NUMBFS_XATTR_MAXVALUE) {
			err = -ERANGE;
			goto out;
		}

		ni->xattr_count += 1;
		xe->e_valid = true;
		xe->e_type = index;
		xe->e_nlen = strlen(name);
		memcpy(xe->e_name, name, xe->e_nlen);
		xe->e_vlen = buffer_size;
		memcpy(xe->e_value, buffer, xe->e_vlen);
		err = numbfs_brw(&buf, NUMBFS_WRITE);
		break;
	}
out:
	numbfs_bput(&buf);
	if (!err)
		mark_inode_dirty(inode);
	return err;

}

static int numbfs_xattrset(const struct xattr_handler *handler,
			    struct mnt_idmap *idmap, struct dentry *dentry,
			    struct inode *inode, const char *name, const void *buffer,
			    size_t size, int flags)
{
	char buf[NUMBFS_BYTES_PER_BLOCK];
	bool exist = true;
	int offset = -1;

	/* make sure that xattr entry exsist */
	if (numbfs_getxattr(inode, handler->flags, name, buf, NUMBFS_BYTES_PER_BLOCK, &offset) == -ENODATA)
		exist = false;

	if ((flags & XATTR_CREATE) && exist)
		return -EEXIST;

	if ((flags & XATTR_REPLACE) && !exist)
		return -ENODATA;

	return numbfs_do_setxattr(inode, handler->flags, name, buffer, size, offset);
}

static bool numbfs_xattr_user_list(struct dentry *dentry)
{
	// TODO: xxx
	return true;
}

static int numbfs_xattr_user_get(const struct xattr_handler *handler,
				 struct dentry *unused, struct inode *inode,
				 const char *name, void *buffer, size_t size)
{
	if (handler->flags != NUMBFS_XATTR_INDEX_USER)
		return -EOPNOTSUPP;

	return numbfs_getxattr(inode, handler->flags, name, buffer, size, NULL);
}

static int numbfs_xattr_user_set(const struct xattr_handler *handler,
				 struct mnt_idmap *idmap, struct dentry *dentry,
				 struct inode *inode, const char *name, const void *buffer,
				 size_t size, int flags)
{
	if (handler->flags != NUMBFS_XATTR_INDEX_USER)
		return -EOPNOTSUPP;

	return numbfs_xattrset(handler, idmap, dentry, inode, name, buffer, size, flags);
}

static bool numbfs_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

static int numbfs_xattr_trusted_get(const struct xattr_handler *handler,
				    struct dentry *unused, struct inode *inode,
				    const char *name, void *buffer, size_t size)
{
	if (handler->flags != NUMBFS_XATTR_INDEX_TRUSTED)
		return -EOPNOTSUPP;

	return numbfs_getxattr(inode, handler->flags, name, buffer, size, NULL);
}

static int numbfs_xattr_trusted_set(const struct xattr_handler *handler,
				    struct mnt_idmap *idmap, struct dentry *dentry,
				    struct inode *inode, const char *name,
				    const void *buffer, size_t size, int flags)
{
	if (handler->flags != NUMBFS_XATTR_INDEX_TRUSTED)
		return -EOPNOTSUPP;

	return numbfs_xattrset(handler, idmap, dentry, inode, name, buffer, size, flags);
}

const struct xattr_handler numbfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= NUMBFS_XATTR_INDEX_USER,
	.list	= numbfs_xattr_user_list,
	.get	= numbfs_xattr_user_get,
	.set	= numbfs_xattr_user_set,
};

const struct xattr_handler numbfs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= NUMBFS_XATTR_INDEX_TRUSTED,
	.list	= numbfs_xattr_trusted_list,
	.get	= numbfs_xattr_trusted_get,
	.set	= numbfs_xattr_trusted_set,
};

const struct xattr_handler * const numbfs_xattr_handlers[] = {
	&numbfs_xattr_user_handler,
	&numbfs_xattr_trusted_handler,
	NULL,
};
