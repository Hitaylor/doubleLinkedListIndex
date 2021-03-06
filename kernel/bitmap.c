/* bitmap.c contains the code that handles the inode and block bitmaps */

#include "episode.h"
#include <linux/buffer_head.h>
#include <linux/bitops.h>
#include <linux/sched.h>

static DEFINE_SPINLOCK(bitmap_lock);

/*
 * bitmap consists of blocks filled with 16bit words
 * bit set == busy, bit clear == free
 * endianness is a mess, but for counting zero bits it really doesn't matter...
 */
static __u32 count_free(struct buffer_head *map[], unsigned blocksize, __u32 numbits)
{
	__u32 sum = 0;
	unsigned blocks = DIV_ROUND_UP(numbits, blocksize * 8);

	while (blocks--) {
		unsigned words = blocksize / 2;
		__u16 *p = (__u16 *)(*map++)->b_data;
		while (words--)
			sum += 16 - hweight16(*p++);
	}

	return sum;
}

void episode_free_block(struct inode *inode, unsigned long block)
{
	struct super_block *sb = inode->i_sb;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long bit, zone;

	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones) {
		printk("Trying to free block not in datazone\n");
		return;
	}
	zone = block - sbi->s_firstdatazone + 1;
	bit = zone & ((1<<k) - 1);
	zone >>= k;
	if (zone >= sbi->s_zmap_blocks) {
		printk("episode_free_block: nonexistent bitmap buffer\n");
		return;
	}
	bh = sbi->s_zmap[zone];
	spin_lock(&bitmap_lock);
	if (!episode_test_and_clear_bit(bit, bh->b_data))
		printk("episode_free_block (%s:%lu): bit already cleared\n",
		       sb->s_id, block);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	return;
}

int episode_new_block(struct inode * inode)
{
	struct episode_sb_info *sbi = episode_sb(inode->i_sb);
	int bits_per_zone = 8 * inode->i_sb->s_blocksize;
	int i;

	for (i = 0; i < sbi->s_zmap_blocks; i++) {
		struct buffer_head *bh = sbi->s_zmap[i];
		int j;

		spin_lock(&bitmap_lock);
		j = episode_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone) {
			episode_set_bit(j, bh->b_data);
			spin_unlock(&bitmap_lock);
			mark_buffer_dirty(bh);
			j += i * bits_per_zone + sbi->s_firstdatazone-1;
			if (j < sbi->s_firstdatazone || j >= sbi->s_nzones)
				break;
			return j;
		}
		spin_unlock(&bitmap_lock);
	}
	return 0;
}

unsigned long episode_count_free_blocks(struct super_block *sb)
{
	struct episode_sb_info *sbi = episode_sb(sb);
	u32 bits = sbi->s_nzones - sbi->s_firstdatazone + 1;

	return (count_free(sbi->s_zmap, sb->s_blocksize, bits)
		<< sbi->s_log_zone_size);
}

/**
 * 这个应该是根据sb，和需要查找的ino号，以及inode链表，查找对应的inode信息。如果缓冲区有ino对应的inode，则返回;否则需要读盘。
 */
struct episode_inode *
episode_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
	int block;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct episode_inode *p;
	int episode_inodes_per_block = sb->s_blocksize / sizeof(struct episode_inode);

	*bh = NULL;
	if (!ino || ino > sbi->s_ninodes) {
		printk("Bad inode number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ino);
		return NULL;
	}
	ino--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ino / episode_inodes_per_block;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read inode block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ino % episode_inodes_per_block;
}

/* Clear the link count and mode of a deleted inode on disk. */

static void episode_clear_inode(struct inode *inode)
{
	struct buffer_head *bh = NULL;

	if (INODE_VERSION(inode) == EPISODE_V) {
		struct episode_inode *raw_inode;
		raw_inode = episode_raw_inode(inode->i_sb, inode->i_ino, &bh);
		if (raw_inode) {
			raw_inode->i_nlinks = 0;
			raw_inode->i_mode = 0;
		}
	}
	if (bh) {
		mark_buffer_dirty(bh);
		brelse (bh);
	}
}

void episode_free_inode(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct episode_sb_info *sbi = episode_sb(inode->i_sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;
	unsigned long ino, bit;

	ino = inode->i_ino;
	if (ino < 1 || ino > sbi->s_ninodes) {
		printk("episode_free_inode: inode 0 or nonexistent inode\n");
		return;
	}
	bit = ino & ((1<<k) - 1);
	ino >>= k;
	if (ino >= sbi->s_imap_blocks) {
		printk("episode_free_inode: nonexistent imap in superblock\n");
		return;
	}

	episode_clear_inode(inode);	/* clear on-disk copy */

	bh = sbi->s_imap[ino];
	spin_lock(&bitmap_lock);
	if (!episode_test_and_clear_bit(bit, bh->b_data))
		printk("episode_free_inode: bit %lu already cleared\n", bit);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
}
/**
 * 创建一个inode，把它放到dir的目录项里面，同时从inode区分配一个空间来存储inode，并对inode进行初始化。
 */
struct inode *episode_new_inode(const struct inode *dir, umode_t mode, int *error)
{
	struct super_block *sb = dir->i_sb;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct inode *inode = new_inode(sb);
	struct buffer_head * bh;
	int bits_per_zone = 8 * sb->s_blocksize;
	unsigned long j;
	int i;

	if (!inode) {
		*error = -ENOMEM;
		return NULL;
	}
	j = bits_per_zone;
	bh = NULL;
	*error = -ENOSPC;
	spin_lock(&bitmap_lock);
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		bh = sbi->s_imap[i];
		j = episode_find_first_zero_bit(bh->b_data, bits_per_zone);
		if (j < bits_per_zone)
			break;
	}
	if (!bh || j >= bits_per_zone) {
		spin_unlock(&bitmap_lock);
		iput(inode);
		return NULL;
	}
	if (episode_test_and_set_bit(j, bh->b_data)) {	/* shouldn't happen */
		spin_unlock(&bitmap_lock);
		printk("episode_new_inode: bit already set\n");
		iput(inode);
		return NULL;
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	j += i * bits_per_zone;
	if (!j || j > sbi->s_ninodes) {
		iput(inode);
		return NULL;
	}
	inode_init_owner(inode, dir, mode);
	inode->i_ino = j;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;
	//上面的都是vfs里面的inode提供的字段，下面的是episode_inode独有的字段，包括i_data[]和i_lastrecordpos
	memset(&episode_i(inode)->u, 0, sizeof(episode_i(inode)->u));
	memset(&episode_i(inode)->i_lastrecordpos,0,sizeof(episode_i(inode)->i_lastrecordpos));//jsc 0520

	insert_inode_hash(inode);//这就体现出来buffer_head的好处了，只是把bh连接起来，而inode的具体类型可以是ext2的，也可以是episode的
	mark_inode_dirty(inode);
	printk("[bitmap.c, episode_new_inode()] ino:%d, i_blocks:%d, ctime:%d, lastrecordpos:%ld\n",inode->i_ino,inode->i_blocks,inode->i_ctime,episode_i(inode)->i_lastrecordpos);
	*error = 0;
	return inode;
}

unsigned long episode_count_free_inodes(struct super_block *sb)
{
	struct episode_sb_info *sbi = episode_sb(sb);
	u32 bits = sbi->s_ninodes + 1;

	return count_free(sbi->s_imap, sb->s_blocksize, bits);
}
