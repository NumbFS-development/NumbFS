// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Hongzhen Luo
 */

#include "internal.h"
#include <linux/pagemap.h>
#include <linux/iomap.h>

#define DOT             "."
#define DOTDOT          ".."
#define DOTLEN          strlen(DOT)
#define DOTDOTLEN       strlen(DOTDOT)

void numbfs_dir_set_ops(struct inode *inode)
{
	inode->i_op             = &numbfs_dir_iops;
	inode->i_fop            = &numbfs_dir_fops;
	inode->i_mapping->a_ops = &numbfs_aops;
}

/* find the target nid according to the name */
static int numbfs_inode_by_name(struct inode *dir, const char *name,
				int namelen, int *nid, int *offset)
{
	struct numbfs_dirent *de;
	struct numbfs_buf buf;
	int i, ret, err, off;

	ret = -ENOENT;
	for (i = 0; i < dir->i_size; i += sizeof(*de)) {
		if (i % NUMBFS_BYTES_PER_BLOCK == 0) {
			if (i > 0)
				numbfs_ibuf_put(&buf);

			numbfs_ibuf_init(&buf, dir, i / NUMBFS_BYTES_PER_BLOCK);
			err = numbfs_ibuf_read(&buf);
			if (err)
				return err;
		}
		off = (i >> NUMBFS_BLOCK_BITS) << NUMBFS_BLOCK_BITS;
		de = (struct numbfs_dirent*)((unsigned char*)buf.base +
				(off & (folio_size(buf.folio) - 1)) +
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

	numbfs_ibuf_put(&buf);
	return ret;
}

static int numbfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *dir = file_inode(file);
	size_t dirsize = i_size_read(dir);
	struct numbfs_buf buf;
	struct numbfs_dirent *de;
	int err, off;

	while (ctx->pos < dirsize) {
		const char *de_name;
		unsigned int de_namelen;
		unsigned char de_type;

		if (ctx->pos % NUMBFS_BYTES_PER_BLOCK == 0) {
			if (ctx->pos > 0)
				numbfs_ibuf_put(&buf);
			numbfs_ibuf_init(&buf, dir,
					 ctx->pos / NUMBFS_BYTES_PER_BLOCK);
			err = numbfs_ibuf_read(&buf);
			if (err) {
				pr_info("numbfs: error to read dir block@%lld, err: %d\n", ctx->pos / NUMBFS_BYTES_PER_BLOCK, err);
				goto out;
			}

		}

		off = (ctx->pos >> NUMBFS_BLOCK_BITS) << NUMBFS_BLOCK_BITS;
		de = (struct numbfs_dirent*)((unsigned char*)buf.base +
				(off & (folio_size(buf.folio) - 1)) +
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
	numbfs_ibuf_put(&buf);
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
	struct numbfs_superblock_info *sbi = NUMBFS_SB(inode->i_sb);
	struct timespec64 now = current_time(inode);
	struct numbfs_buf buf;
	int i, blk;

	inode->i_ino = nid;
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = dir->i_gid;
	set_nlink(inode, S_ISDIR(mode) ? 2 : 1);
	inode->i_size = 0;
	inode->i_blocks = 0;

	inode_set_atime(inode, now.tv_sec, now.tv_nsec);
	inode_set_ctime(inode, now.tv_sec, now.tv_nsec);
	inode_set_mtime(inode, now.tv_sec, now.tv_nsec);

	if (S_ISDIR(mode))
		numbfs_dir_set_ops(inode);
	else
		numbfs_file_set_ops(inode);

	ni->sbi = NUMBFS_SB(inode->i_sb);
	ni->nid = nid;
	for (i = 0; i < NUMBFS_NUM_DATA_ENTRY; i++)
		ni->data[i] = NUMBFS_HOLE;

	blk = -1;
	(void)numbfs_balloc(inode->i_sb, &blk);

	/* zero out this block */
	(void)numbfs_binit(&buf, inode->i_sb->s_bdev, numbfs_data_blk(sbi, blk));
	(void)numbfs_brw(&buf, NUMBFS_READ);
	memset(buf.base, 0, NUMBFS_BYTES_PER_BLOCK);
	(void)numbfs_brw(&buf, NUMBFS_WRITE);

	ni->xattr_start = blk;
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

static int numbfs_make_empty(struct inode *dir, struct inode *pdir,
			     umode_t mode, int nid)
{
	int err;

	err = numbfs_write_dir(dir, mode, DOT, DOTLEN, nid, 0);
	if (err)
		return err;

	return numbfs_write_dir(dir, pdir->i_mode, DOTDOT, DOTDOTLEN,
				pdir->i_ino, 0);
}

static int numbfs_dir_mkdir(struct mnt_idmap *idmap, struct inode *dir,
			    struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	int err, nid;

	if (numbfs_inode_by_name(dir, dentry->d_name.name,
			dentry->d_name.len, &nid, NULL) != -ENOENT)
		return -EEXIST;

	mode |= S_IFDIR;
	inode = numbfs_dir_ialloc(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	d_instantiate_new(dentry, inode);

	err = numbfs_make_empty(inode, dir, mode, inode->i_ino);
	if (err)
		return err;

	return numbfs_write_dir(dir, mode, name, namelen, inode->i_ino, 0);
}

static int numbfs_delete_entry(struct inode *dir, int nid, int offset)
{
	struct folio *folio, *last_folio;
	struct numbfs_dirent *de_from, *de_to;
	void *kaddr_from, *kaddr_to;
	int off_from, off_to;
	int size = i_size_read(dir);

	folio = read_cache_folio(dir->i_mapping, offset >> PAGE_SHIFT,
				 NULL, NULL);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	last_folio = read_cache_folio(dir->i_mapping, size >> PAGE_SHIFT,
				      NULL, NULL);
	if (IS_ERR(last_folio))
		return PTR_ERR(last_folio);

	folio_lock(folio);
	kaddr_to = kmap_local_folio(folio, 0);
	kaddr_from = kmap_local_folio(last_folio, 0);
	off_from = (size & (folio_size(last_folio) - 1)) - sizeof(*de_from);
	off_to = (offset & (folio_size(folio) - 1));
	de_from = (struct numbfs_dirent*)(kaddr_from + off_from);
	de_to = (struct numbfs_dirent*)(kaddr_to + off_to);
	memcpy(de_to, de_from, sizeof(struct numbfs_dirent));

	iomap_dirty_folio(dir->i_mapping, folio);
	folio_unlock(folio);

	folio_release_kmap(folio, kaddr_to);
	folio_release_kmap(last_folio, kaddr_from);

	numbfs_setsize(dir, size - sizeof(struct numbfs_dirent));
	mark_inode_dirty(dir);

	return 0;
}

static int numbfs_dir_unlink(struct inode *dir, struct dentry *dentry)
{
	int nid, offset, err;

	err = numbfs_inode_by_name(dir, dentry->d_name.name,
			dentry->d_name.len, &nid, &offset);
	if (err < 0) {
		if (err == -ENOENT)
			return 0;
		return err;
	}

	err = numbfs_delete_entry(dir, nid, offset);
	if (err)
		return err;

	inode_dec_link_count(d_inode(dentry));
	return 0;
}

static bool numbfs_is_empty(struct inode *dir)
{
	int err, nid, offset;

	BUG_ON(i_size_read(dir) < 2 * sizeof(struct numbfs_dirent));

	if (i_size_read(dir) != 2 * sizeof(struct numbfs_dirent))
		return false;

	err = numbfs_inode_by_name(dir, DOT, DOTLEN, &nid, &offset);
	if (err)
		return false;

	err = numbfs_inode_by_name(dir, DOTDOT, DOTDOTLEN, &nid, &offset);
	if (err)
		return false;

	return true;
}

static int numbfs_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	int offset, nid;
	int err;

	err = numbfs_inode_by_name(dir, dentry->d_name.name,
			dentry->d_name.len, &nid, &offset);
	if (err < 0) {
		if (err == -ENOENT)
			return 0;
		return err;
	}

	err = -ENOTEMPTY;
	if (numbfs_is_empty(d_inode(dentry))) {
		err = numbfs_delete_entry(dir, nid, offset);
		if (err)
			return err;
		inode_dec_link_count(d_inode(dentry));
		inode_dec_link_count(d_inode(dentry));
	}
	return err;
}

static int numbfs_dir_rename(struct mnt_idmap *idmap,
		struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry,
		unsigned int flags)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct numbfs_dirent de;
	struct numbfs_buf buf;
	int err, nid, offset;

	/* delete the dirent in new_dir */
	if (new_inode) {
		if (S_ISDIR(new_inode->i_mode))
			err = numbfs_dir_rmdir(new_dir, new_dentry);
		else
			err = numbfs_dir_unlink(new_dir, new_dentry);
		if (err)
			return err;
	}

	/* delete the dirent in old_dir */
	err = numbfs_inode_by_name(old_dir, old_dentry->d_name.name,
			old_dentry->d_name.len, &nid, &offset);
	if (err)
		return err;

	numbfs_ibuf_init(&buf, old_dir, offset >> NUMBFS_BLOCK_BITS);
	err = numbfs_ibuf_read(&buf);
	if (err)
		return err;
	memcpy(&de, (char*)buf.base + (offset & (folio_size(buf.folio)- 1)),
	       sizeof(struct numbfs_dirent));
	numbfs_ibuf_put(&buf);

	err = numbfs_delete_entry(old_dir, nid, offset);
	if (err)
		return err;

	/* append the dirent in new_dir */
	err = numbfs_write_dir(new_dir, old_inode->i_mode, new_dentry->d_name.name,
			new_dentry->d_name.len, le16_to_cpu(de.ino), 0);
	if (err)
		return err;

	/* if dirent is dir, change the ".." */
	if (de.type == DT_DIR) {
		struct inode *target;

		target = numbfs_iget(old_inode->i_sb, le16_to_cpu(de.ino));
		if (IS_ERR(target))
			return PTR_ERR(target);

		err = numbfs_inode_by_name(target, DOTDOT, DOTDOTLEN, &nid, &offset);
		if (err)
			return err;

		err = numbfs_write_dir(target, S_IFDIR, DOTDOT, DOTDOTLEN,
				       new_dir->i_ino, offset);
		iput(target);
		if (err)
			return err;
	}
	return 0;
}

static int numbfs_dir_link(struct dentry *old_dentry, struct inode *dir,
			   struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	int err, nid, off;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (dir->i_sb != inode->i_sb)
		return -EXDEV;

	err = numbfs_inode_by_name(dir, name, namelen, &nid, &off);
	if (err != -ENOENT)
		return -EEXIST;

	inode_inc_link_count(inode);
	ihold(inode);

	err = numbfs_write_dir(dir, inode->i_mode & S_IFMT, name,
			       namelen, inode->i_ino, 0);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}

	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static int numbfs_dir_symlink(struct mnt_idmap *idmap, struct inode *dir,
			      struct dentry * dentry, const char * symname)
{
	struct inode *inode;
	struct folio *folio;
	int err, nid, off;
	void *kaddr;

	if (strlen(symname) > NUMBFS_BYTES_PER_BLOCK)
		return -ENAMETOOLONG;

	err = numbfs_inode_by_name(dir, dentry->d_name.name,
				   dentry->d_name.len, &nid, &off);
	if (err != -ENOENT)
		return -EEXIST;

	/* alloc inode */
	inode = numbfs_dir_ialloc(dir, S_IFLNK | 0444);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	/* copy symname */
	folio = read_cache_folio(inode->i_mapping, 0, NULL, NULL);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	folio_lock(folio);
	kaddr = kmap_local_folio(folio, 0);
	memcpy(kaddr, symname, strlen(symname));
	iomap_dirty_folio(inode->i_mapping, folio);
	folio_unlock(folio);

	folio_release_kmap(folio, kaddr);
	numbfs_setsize(inode, strlen(symname));
	mark_inode_dirty(inode);

	/* instantiate inode and dentry */
	d_instantiate_new(dentry, inode);

	/* append dirent */
	return numbfs_write_dir(dir, S_IFLNK, dentry->d_name.name,
				dentry->d_name.len, inode->i_ino, 0);
}

const struct inode_operations numbfs_dir_iops = {
	.lookup         = numbfs_dir_lookup,
	.create         = numbfs_dir_create,
	.mkdir          = numbfs_dir_mkdir,
	.unlink         = numbfs_dir_unlink,
	.rmdir          = numbfs_dir_rmdir,
	.rename         = numbfs_dir_rename,
	.link           = numbfs_dir_link,
	.symlink        = numbfs_dir_symlink,
};

const struct file_operations numbfs_dir_fops = {
	.llseek         = generic_file_llseek,
	.read           = generic_read_dir,
	.iterate_shared = numbfs_readdir,
};
