#ifndef FS_episode_H
#define FS_episode_H

#include "episode_fs.h"
#include <linux/fs.h>
#include <linux/pagemap.h>

#define INODE_VERSION(inode)	episode_sb(inode->i_sb)->s_version
#define EPISODE_V		0x0001		/* original episode fs */

/*
 * episode fs inode data in memory
 */
struct episode_inode_info {
	__u64 i_lastrecordpos;//jsc
	union {
		__u16 i1_data[16];
		__u32 i2_data[16];
	} u;
	struct inode vfs_inode;
};

/*
 * episode super-block data in memory
 */
struct episode_sb_info {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_imap_blocks;
	unsigned long s_zmap_blocks;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	int s_dirsize;
	int s_namelen;
	struct buffer_head ** s_imap;
	struct buffer_head ** s_zmap;
	struct buffer_head * s_sbh;
	struct episode_super_block * s_es;
	unsigned short s_mount_state;
	unsigned short s_version;
};

extern struct inode *episode_iget(struct super_block *, unsigned long);
extern struct episode_inode * episode_raw_inode(struct super_block *, ino_t, struct buffer_head **);
extern struct inode * episode_new_inode(const struct inode *, umode_t, int *);
extern void episode_free_inode(struct inode * inode);
extern unsigned long episode_count_free_inodes(struct super_block *sb);
extern int episode_new_block(struct inode * inode);
extern void episode_free_block(struct inode *inode, unsigned long block);
extern unsigned long episode_count_free_blocks(struct super_block *sb);
extern int episode_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int episode_prepare_chunk(struct page *page, loff_t pos, unsigned len);

extern void episode_truncate(struct inode *);
extern void episode_set_inode(struct inode *, dev_t);
extern int episode_get_block(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create);
//extern int __episode_get_block(struct inode *, long, struct buffer_head *, int);
extern unsigned episode_blocks(loff_t, struct super_block *);
extern void episode_write_failed(struct address_space *mapping, loff_t to);

extern struct episode_dir_entry *episode_find_entry(struct dentry*, struct page**);
extern int episode_add_link(struct dentry*, struct inode*);
extern int episode_delete_entry(struct episode_dir_entry*, struct page*);
extern int episode_make_empty(struct inode*, struct inode*);
extern int episode_empty_dir(struct inode*);
extern void episode_set_link(struct episode_dir_entry*, struct page*, struct inode*);
extern struct episode_dir_entry *episode_dotdot(struct inode*, struct page**);
extern ino_t episode_inode_by_name(struct dentry*);

extern void left(char *dst,char *src, int n);
extern void mid_char(char *dst,char *src, int n, int m);//字符串截取函数
extern void right(char *dst,char *src, int n);
extern int getCurrentTime(void);

extern void mid(void *dest, void *src, int n, int m);//任意类型截取函数

extern void int32tochar(int32_t num, char ch[sizeof(int32_t)]);
extern void chartoint32(int32_t *num, char ch[sizeof(int32_t)]);

extern const struct inode_operations episode_file_inode_operations;
extern const struct inode_operations episode_dir_inode_operations;
extern const struct file_operations episode_file_operations;
extern const struct file_operations episode_dir_operations;

static inline struct episode_sb_info *episode_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct episode_inode_info *episode_i(struct inode *inode)
{
	return container_of(inode, struct episode_inode_info, vfs_inode);
}

static inline unsigned episode_blocks_needed(unsigned bits, unsigned blocksize)
{
	return DIV_ROUND_UP(bits, blocksize * 8);
}
/*
获取inode中记录的上一条记录的位置。jsc
*/
static inline __u64 i_lastrecordpos(struct inode *inode)
{
	return episode_i(inode)->i_lastrecordpos;
};

#if defined(CONFIG_EPISODE_FS_NATIVE_ENDIAN) && \
	defined(CONFIG_EPISODE_FS_BIG_ENDIAN_16BIT_INDEXED)

#error episode file system byte order broken

#elif defined(CONFIG_EPISODE_FS_NATIVE_ENDIAN)

/*
 * big-endian 32 or 64 bit indexed bitmaps on big-endian system or
 * little-endian bitmaps on little-endian system
 */

#define episode_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr), (unsigned long *)(addr))
#define episode_set_bit(nr, addr)		\
	__set_bit((nr), (unsigned long *)(addr))
#define episode_test_and_clear_bit(nr, addr) \
	__test_and_clear_bit((nr), (unsigned long *)(addr))
#define episode_test_bit(nr, addr)		\
	test_bit((nr), (unsigned long *)(addr))
#define episode_find_first_zero_bit(addr, size) \
	find_first_zero_bit((unsigned long *)(addr), (size))

#elif defined(CONFIG_EPISODE_FS_BIG_ENDIAN_16BIT_INDEXED)

/*
 * big-endian 16bit indexed bitmaps
 */

static inline int episode_find_first_zero_bit(const void *vaddr, unsigned size)
{
	const unsigned short *p = vaddr, *addr = vaddr;
	unsigned short num;

	if (!size)
		return 0;

	size >>= 4;
	while (*p++ == 0xffff) {
		if (--size == 0)
			return (p - addr) << 4;
	}

	num = *--p;
	return ((p - addr) << 4) + ffz(num);
}

#define episode_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr) ^ 16, (unsigned long *)(addr))
#define episode_set_bit(nr, addr)	\
	__set_bit((nr) ^ 16, (unsigned long *)(addr))
#define episode_test_and_clear_bit(nr, addr)	\
	__test_and_clear_bit((nr) ^ 16, (unsigned long *)(addr))

static inline int episode_test_bit(int nr, const void *vaddr)
{
	const unsigned short *p = vaddr;
	return (p[nr >> 4] & (1U << (nr & 15))) != 0;
}

#else

/*
 * little-endian bitmaps
 */

#define episode_test_and_set_bit	__test_and_set_bit_le
#define episode_set_bit		__set_bit_le
#define episode_test_and_clear_bit	__test_and_clear_bit_le
#define episode_test_bit	test_bit_le
#define episode_find_first_zero_bit	find_first_zero_bit_le

#endif

#endif /* FS_episode_H */
