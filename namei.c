// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext2/namei.c
 *
 * Rewrite to pagecache. Almost all code had been changed, so blame me
 * if the things go wrong. Please, send bug reports to
 * viro@parcelfarce.linux.theplanet.co.uk
 *
 * Stuff here is basically a glue between the VFS and generic UNIXish
 * filesystem that keeps everything in pagecache. All knowledge of the
 * directory layout is in fs/ext2/dir.c - it turned out to be easily separatable
 * and it's easier to debug that way. In principle we might want to
 * generalize that a bit and turn it into a library. Or not.
 *
 * The only non-static object here is ext2_dir_inode_operations.
 *
 * TODO: get rid of kmap() use, add readahead.
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

// 不知道这个函数是干嘛的
static inline int ext2_add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = ext2_add_link(dentry, inode);
	if (!err) {
		d_instantiate_new(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	discard_new_inode(inode);
	return err;
}

/*
 * Methods themselves.
 */
// 从 dir 指定的inode节点的数据内容所在的磁盘上查找与dentry相匹配的dentry?
// 参数的dentry只是一个dentry空壳, 只包含了一些必要的信息
// 这个函数当我么需要根据路径名查找时, 就先构造一个dentry, 但是这个dentry只初始化来名字字段, 其他字段交给这个函数处理
static struct dentry *ext2_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode;
	// ino_t 就是unsigned long类型
	ino_t ino;
	
	if (dentry->d_name.len > EXT2_NAME_LEN)// > 255
		return ERR_PTR(-ENAMETOOLONG);

	// ext2_inode_by_name 就是根据目录的vfs inode对象来读取磁盘, 解析ext2_dentry, 并最终获得&dentry->d_name的inode编号
	ino = ext2_inode_by_name(dir, &dentry->d_name);
	inode = NULL;
	if (ino) {
		inode = ext2_iget(dir->i_sb, ino);
		if (inode == ERR_PTR(-ESTALE)) {
			ext2_error(dir->i_sb, __func__,
					"deleted inode referenced: %lu",
					(unsigned long) ino);
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(inode, dentry);
}

struct dentry *ext2_get_parent(struct dentry *child)
{
	struct qstr dotdot = QSTR_INIT("..", 2);
	unsigned long ino = ext2_inode_by_name(d_inode(child), &dotdot);
	if (!ino)
		return ERR_PTR(-ENOENT);
	return d_obtain_alias(ext2_iget(child->d_sb, ino));
} 

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate(). 
 */

// 这个函数的目的是在参数dir下面创建文件的inode节点, 其中dentry是该文件的dentry条目
// 创建的vfs inode会被插入sb的inode链表, 磁盘数据结构 ext2_inode 会卸乳磁盘的高速缓存页, 并将该页标记为脏
static int ext2_create (struct inode * dir, struct dentry * dentry, umode_t mode, bool excl)
{
	struct inode *inode;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	// 在dir指定的目录下创建 inode, 并将创建的磁盘数据结构写到磁盘
	// 返回的inode是 vfs 的 inode
	inode = ext2_new_inode(dir, mode, &dentry->d_name);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	// 根据挂载时候的选项以及访问文件的选项设置inode节点的操作inode的方法和file的方法
	// 必须先create以后, 才能设置 file option
	// 因为 ext2 根据文件的各种标志和挂载系统的一些标志, 可以在加载文件的时候动态设置文件以及inode的操作函数
	// 所以不需要将这些字段保存到磁盘, 也不能将这些字段保存到磁盘, 因为这样可能带来很繁琐的移植性问题(比如在linuxA 上好用,但是在LinuxB上就不好用, 是因为他们内存中的函数地址不一样)
	ext2_set_file_ops(inode);
	mark_inode_dirty(inode);
        // 这个函数大概就是来写磁盘dentry的
	// 明天在看看
	// 网上说这个函数是建立dentry和inode的连接, 
	// 因伪在申请磁盘上的数据结构 ext2_inode 之前, 就已经申请完该文件的dentry了
	// 但是该dentry是内存数据结构, 是通过下面的这个函数来将他写入磁盘数据结构 ext2_dentry
	return ext2_add_nondir(dentry, inode);
	return 0;
}

// 在dir目录下创建一个临时文件
static int ext2_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode = ext2_new_inode(dir, mode, NULL);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	ext2_set_file_ops(inode);
	mark_inode_dirty(inode);
	// 是根据inode节点的名字来为临时文件命名的
	d_tmpfile(dentry, inode);
	unlock_new_inode(inode);
	return 0;
}

// 该函数被系统调用mknod()调用，对应于shell命令mknod
// 创建特殊文件(设备文件、命名管道或套接字)。
// 要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定
// dev_t其实就是一个u32, 只不过程序把他分为12bit的主设备, 20bit的次设备
static int ext2_mknod (struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode * inode;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode = ext2_new_inode (dir, mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		// 下面这条语句就是根据i_mode的类型(是字符设备还是块设备还是fifo)
		// 来设置inode的i_op字段
		init_special_inode(inode, inode->i_mode, rdev);
#ifdef CONFIG_EXT2_FS_XATTR
		// 使用自己独有的operation
		// 在这里是ext2_dir_inode_operations的一个子集
		inode->i_op = &ext2_special_inode_operations;
#endif
		mark_inode_dirty(inode);
		// 是根据 inode 的信息补充 dentry的信息
		err = ext2_add_nondir(dentry, inode);
	}
	return err;
}

// 为 dir 目录下的 dentry 目录项创建一个符号链接?
// 应该是为符号链接创建一个新的索引点
static int ext2_symlink (struct inode * dir, struct dentry * dentry,
	const char * symname)
{
	struct super_block * sb = dir->i_sb;
	int err = -ENAMETOOLONG;
	unsigned l = strlen(symname)+1;
	struct inode * inode;

	if (l > sb->s_blocksize)
		goto out;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	inode = ext2_new_inode (dir, S_IFLNK | S_IRWXUGO, &dentry->d_name);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out;

	// 需要分配新的磁盘块
	if (l > sizeof (EXT2_I(inode)->i_data)) {
		/* slow symlink */
		inode->i_op = &ext2_symlink_inode_operations;
		inode_nohighmem(inode);
		// mount 的时候使用了 no buffer head
		if (test_opt(inode->i_sb, NOBH))
			// 设置的是 address_space_options
			inode->i_mapping->a_ops = &ext2_nobh_aops;
		else
			inode->i_mapping->a_ops = &ext2_aops;
		err = page_symlink(inode, symname, l);
		if (err)
			goto out_fail;
	} else {
		// 不需要分配新的磁盘块, 直接放在 ext2_inode 的 i_block 字段
		/* fast symlink */
		inode->i_op = &ext2_fast_symlink_inode_operations;
		// 这个 link 就直接存放的目标文件的文件名
		// 他用来存放这些字符串的地址是 ext2_inode_info 的 i_data 数组
		inode->i_link = (char*)EXT2_I(inode)->i_data;
		memcpy(inode->i_link, symname, l);
		inode->i_size = l-1;
	}
	// 注意: ext2_inode 并不作为 ext2_inode_info 或者 inode 对象的一部分
	// 而是ext2_inode 与 ext2_inode_info 之间存在一一映射关系
	mark_inode_dirty(inode);

	err = ext2_add_nondir(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	discard_new_inode(inode);
	goto out;
}

// 创建一个新的名为dentry硬链接，这个新的硬连接指向dir目录下名为的old_dentry文件
// 所以应该增加 old_dentry 对应的inode节点的链接数
static int ext2_link (struct dentry * old_dentry, struct inode * dir,
	struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode->i_ctime = current_time(inode);
	// 增加vfs inode结构的 i_nlink 字段, 并将 inode 标记为脏, i_nlink 对应着硬连接的个数
	inode_inc_link_count(inode);
	// 增加vfs inode结构的 i_count 字段, 这个字段是放在内存使用, 如果该字段为0, 内存紧张的时候就可以放回该字段
	ihold(inode);

	// 将 inode 与其对应的 dentry 链接到一起, 并写磁盘
	// inode 应该是 dentry 对应的磁盘 vfs_inode
	err = ext2_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

// 在 dir 下, 创建一个目录
static int ext2_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	struct inode * inode;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	inode_inc_link_count(dir);

	// 创建 dentry 对应的 inode
	inode = ext2_new_inode(dir, S_IFDIR | mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	// 设置该目录节点的 inode operation 和 file operation
	inode->i_op = &ext2_dir_inode_operations;
	inode->i_fop = &ext2_dir_operations;
	// 这个是根据挂载的一些信息设置 address_space_operations
	if (test_opt(inode->i_sb, NOBH))
		inode->i_mapping->a_ops = &ext2_nobh_aops;
	else
		inode->i_mapping->a_ops = &ext2_aops;

	inode_inc_link_count(inode);

	// 就是创建 . 和 .. 这两个目录, 具体细节没看
	err = ext2_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = ext2_add_link(dentry, inode);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	discard_new_inode(inode);
out_dir:
	inode_dec_link_count(dir);
	goto out;
}

// 从dir目录删除dentry目录项所指文件的硬链接
// 如果 i_nlink 为0, 就可以删除相应文件了,
// 对应于系统调用 unlink
static int ext2_unlink(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	struct ext2_dir_entry_2 * de;
	struct page * page;
	int err;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	// page 最终会返回 de 所在的 page 地址
	de = ext2_find_entry (dir, &dentry->d_name, &page);
	if (!de) {
		err = -ENOENT;
		goto out;
	}

	// 这个删除过程还是比较有意思的
	err = ext2_delete_entry (de, page);
	if (err)
		goto out;

	inode->i_ctime = dir->i_ctime;
	// 这个函数才会真正的减少链接计数
	// 但是也没有真正的删除文件啊。。。
	inode_dec_link_count(inode);
	err = 0;
out:
	return err;
}

// 只能删除空目录
static int ext2_rmdir (struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	// 判断目录是否为空
	if (ext2_empty_dir(inode)) {
		err = ext2_unlink(dir, dentry);
		if (!err) {
			inode->i_size = 0;
			// 减少inode的链接计数对应的是 .
			inode_dec_link_count(inode);
			// 减少dir的链接计数对应的是..
			inode_dec_link_count(dir);
		}
	}
	return err;
}

// 之所以需要这么多的参数是因为不仅仅是通常意义的rename 还实现了move的操作, 可以与命令mv对应
// VFS调用该函数来移动文件。文件源路径在old_dir目录中，源文件由old_dentry目录项所指定，
// 目标路径在new_dir目录中，目标文件由new_dentry指定
static int ext2_rename (struct inode * old_dir, struct dentry * old_dentry,
			struct inode * new_dir,	struct dentry * new_dentry,
			unsigned int flags)
{
	struct inode * old_inode = d_inode(old_dentry);
	struct inode * new_inode = d_inode(new_dentry);
	struct page * dir_page = NULL;
	struct ext2_dir_entry_2 * dir_de = NULL;
	struct page * old_page;
	struct ext2_dir_entry_2 * old_de;
	int err;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	err = dquot_initialize(old_dir);
	if (err)
		goto out;

	err = dquot_initialize(new_dir);
	if (err)
		goto out;

	// old_de 是 ext2_dir_entry_2 类型的指针
	old_de = ext2_find_entry (old_dir, &old_dentry->d_name, &old_page);
	if (!old_de) {
		err = -ENOENT;
		goto out;
	}

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		// 获取所在目录的 ext2_dir_entry_2
		dir_de = ext2_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	// 表示目标文件已经存在
	if (new_inode) {
		struct page *new_page;
		struct ext2_dir_entry_2 *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !ext2_empty_dir (new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = ext2_find_entry (new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		ext2_set_link(new_dir, new_de, new_page, old_inode, 1);
		new_inode->i_ctime = current_time(new_inode);
		// 删除new_inode, 也就是说 mv a.txt b.txt, 但是 a.txt b.txt 都存在
		// 那么会删除 b.txt(这里的删除也就是通常意义的rm, 如果该节点有多个硬连接, 并不会删除文件),
		// 只是将文件的链接计数减一, 也就是用的 drop_nlink 的方法
		if (dir_de)
			// 源文件是目录的话就需要将link数减2, 普通文件减一
			// drop_nlink 和 inode_dec_link_count 都是将__i_nlink减1, 但是drop_nlink不mark_inode_dirty
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else 
		// 表示目标文件不存在，例如: mv a.txt b.txt, b.txt原先不存在
		{
		// 也就是在新目录下建一个目录项, 将old_inode链接过去就可以了
		err = ext2_add_link(new_dentry, old_inode);
		if (err)
			goto out_dir;
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
 	 * rename.
	 */
	old_inode->i_ctime = current_time(old_inode);
	mark_inode_dirty(old_inode);

	ext2_delete_entry (old_de, old_page);

	if (dir_de) {
		if (old_dir != new_dir)
			ext2_set_link(old_inode, dir_de, dir_page, new_dir, 0);
		else {
			kunmap(dir_page);
			put_page(dir_page);
		}
		inode_dec_link_count(old_dir);
	}
	return 0;


out_dir:
	if (dir_de) {
		kunmap(dir_page);
		put_page(dir_page);
	}
out_old:
	kunmap(old_page);
	put_page(old_page);
out:
	return err;
}

const struct inode_operations ext2_dir_inode_operations = {
	.create		= ext2_create,
	.lookup		= ext2_lookup,
	.link		= ext2_link,
	.unlink		= ext2_unlink,
	.symlink	= ext2_symlink,
	.mkdir		= ext2_mkdir,
	.rmdir		= ext2_rmdir,
	.mknod		= ext2_mknod,
	.rename		= ext2_rename,
	// xattr 相关的函数定义在xattr.c中
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
	// attr 相关的函数定义在inode.c中
	.setattr	= ext2_setattr,
	// acl 相关的定义在 acl.c 中
	.get_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
	.tmpfile	= ext2_tmpfile,
};

const struct inode_operations ext2_special_inode_operations = {
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
};
