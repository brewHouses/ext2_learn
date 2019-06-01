// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext2/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 symlink handling code
 */

#include "ext2.h"
#include "xattr.h"

const struct inode_operations ext2_symlink_inode_operations = {
	.get_link	= page_get_link,
	.setattr	= ext2_setattr,
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
};
 
const struct inode_operations ext2_fast_symlink_inode_operations = {
	// 只有simple_get_link需要看
	// 就是很简单的将 vfs inode 的i_link 字段返回
	.get_link	= simple_get_link,
	.setattr	= ext2_setattr,
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
};
