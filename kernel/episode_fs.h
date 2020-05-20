#ifndef _LINUX_EPISODE_FS_H
#define _LINUX_EPISODE_FS_H

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

#define EPISODE_BLOCK_SIZE_BITS 12
#define EPISODE_BLOCK_SIZE     (1 << EPISODE_BLOCK_SIZE_BITS)

#define EPISODE_INODES_PER_BLOCK ((EPISODE_BLOCK_SIZE)/(sizeof (struct episode_inode)))

#define EPISODE_SUPER_MAGIC 0xeeee

/*
 * The episode inode has all the time entries, as well as
 * long block numbers and a third indirect block (7+1+1+1
 * instead of 7+1+1). Also, some previously 8-bit values are
 * 16-bit. The inode is 64 bytes.-->128 bytes
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
#endif
