#ifndef UTIL_LINUX_EPISODE_PROGRAMS_H
#define UTIL_LINUX_EPISODE_PROGRAMS_H

#include <linux/types.h>
#include <linux/magic.h>

/*
 * The episode filesystem constants/structures
 */

#define EPISODE_ROOT_INO 1

/* Not the same as the bogus LINK_MAX in <linux/limits.h>. Oh well. */
//#define EPISODE_LINK_MAX	65530

//#define EPISODE_I_MAP_SLOTS	8
//#define EPISODE_Z_MAP_SLOTS	64
#define EPISODE_VALID_FS		0x0001		/* Clean fs. */
#define EPISODE_ERROR_FS		0x0002		/* fs has errors. */

#define EPISODE_INODES_PER_BLOCK ((EPISODE_BLOCK_SIZE)/(sizeof (struct episode_inode)))

#define EPISODE_SUPER_MAGIC 0xeeee

/*
 * The episode inode has all the time entries, as well as
 * long block numbers and a third indirect block (7+1+1+1
 * instead of 7+1+1). Also, some previously 8-bit values are
 * 16-bit. The inode is 64 bytes.
 */
struct episode_inode {
	__u16 i_mode;
	__u16 i_nlinks;
	__u16 i_uid;
	__u16 i_gid;
	__u32 i_size;
	__u32 i_atime;
	__u32 i_mtime;
	__u32 i_ctime;
	__u32 i_zone[10];
	__u64 i_lastrecordpos;//jsc
	__u64 i_padd0[7];//jsc
};

/*
 * episode super-block data on disk
 */
struct episode_super_block {
	__u32 s_ninodes;
	__u16 s_pad0;
	__u16 s_imap_blocks;
	__u16 s_zmap_blocks;
	__u16 s_firstdatazone;
	__u16 s_log_zone_size;
	__u16 s_pad1;
	__u64 s_max_size;
	__u32 s_zones;
	__u16 s_magic;
	__u16 s_pad2;
	__u16 s_blocksize;
	__u8  s_disk_version;
};

struct episode_dir_entry {
	__u32 inode;
	char name[0];
};

/*
 * episode subpartitions are always within primary dos partition.
 */
//#define EPISODE_MAXPARTITIONS  4

#define EPISODE_BLOCK_SIZE_BITS 12
#define EPISODE_BLOCK_SIZE     (1 << EPISODE_BLOCK_SIZE_BITS)

#define EPISODE_NAME_MAX       255             /* # chars in a file name */
//#define EPISODE_MAX_INODES     65535

extern char *super_block_buffer;

#define Super (*(struct episode_super_block *) super_block_buffer)

#define INODE_SIZE (sizeof(struct episode_inode))

#define BITS_PER_BLOCK (EPISODE_BLOCK_SIZE << 3)

#define UPPER(size,n) ((size+((n)-1))/(n))

/*
 * Inline functions.
 */
static inline unsigned long get_ninodes(void)
{
	return Super.s_ninodes;
}

static inline unsigned long get_nzones(void)
{
	return (unsigned long)Super.s_zones;
}

static inline unsigned long get_nimaps(void)
{

	return (unsigned long)Super.s_imap_blocks;

}

static inline unsigned long get_nzmaps(void)
{
	return (unsigned long)Super.s_zmap_blocks;
}

static inline unsigned long get_first_zone(void)
{
	return (unsigned long)Super.s_firstdatazone;
}

static inline unsigned long get_zone_size(void)
{
	
	return (unsigned long)Super.s_log_zone_size;
}

static inline unsigned long get_max_size(void)
{
	
	return (unsigned long)Super.s_max_size;

}

static unsigned long inode_blocks(void)
{

	return UPPER(get_ninodes(), EPISODE_INODES_PER_BLOCK);

}

static inline unsigned long first_zone_data(void)
{
	return 2 + get_nimaps() + get_nzmaps() + inode_blocks();
}

static inline unsigned long get_inode_buffer_size(void)
{
	return inode_blocks() * EPISODE_BLOCK_SIZE;
}

#endif				/* UTIL_LINUX_EPISODE_PROGRAMS_H */
