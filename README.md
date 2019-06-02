## 如何阅读 ext2 的源代码

- 建议先阅读 ext2.h, 了解使用到的数据结构
- 然后根据 opterations 中的函数实现来了解每一个函数

### operations 的分类

- inode_operations

'''c
ext2_file_inode_operations
ext2_dir_inode_operations
ext2_special_inode_operations
ext2_fast_symlink_inode_operations
ext2_symlink_inode_operations
'''

- file_operations

'''c
ext2_dir_operations
ext2_file_operations
'''

- address_space_operations

'''c
ext2_aops
ext2_nobh_aops
ext2_dax_aops
'''
