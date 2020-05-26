/*
 * mkfs.episode.c - make a linux (episode) file-system.
 *
 *
 */

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/stat.h>
#include <mntent.h>
#include <getopt.h>
#include <err.h>

#include "blkdev.h"
#include "episode_programs.h"
#include "nls.h"
#include "pathnames.h"
#include "bitops.h"
#include "exitcodes.h"
#include "strutils.h"
#include "writeall.h"

#define EPISODE_ROOT_INO 1

//#define TEST_BUFFER_BLOCKS 16
#define MAX_GOOD_BLOCKS 512

/*
 * Global variables used in episode_programs.h inline fuctions
 */
char *super_block_buffer;

static char *inode_buffer = NULL;
#define Inode (((struct episode_inode *) inode_buffer) - 1)
static char *program_name = "mkfs";
static char *device_name = NULL;
static int DEV = -1;
static unsigned long long BLOCKS;

static size_t dirsize = 64;
static int magic = EPISODE_SUPER_MAGIC;
static char root_block[EPISODE_BLOCK_SIZE];

static char boot_block_buffer[512];

static unsigned short good_blocks_table[MAX_GOOD_BLOCKS];
static int used_good_blocks = 0;
static unsigned long req_nr_inodes;

static char *inode_map;
static char *zone_map;

#define zone_in_use(x) (isset(zone_map,(x)-get_first_zone()+1) != 0)

#define mark_inode(x) (setbit(inode_map,(x)))
#define unmark_inode(x) (clrbit(inode_map,(x)))

#define mark_zone(x) (setbit(zone_map,(x)-get_first_zone()+1))
#define unmark_zone(x) (clrbit(zone_map,(x)-get_first_zone()+1))


static void __attribute__((__noreturn__))
usage(void) {
	errx(MKFS_USAGE, _("Usage: %s /dev/name"), program_name);
}

static void check_mount(void) {
	FILE * f;
	struct mntent * mnt;

	if ((f = setmntent (_PATH_MOUNTED, "r")) == NULL)
		return;
	while ((mnt = getmntent (f)) != NULL)
		if (strcmp (device_name, mnt->mnt_fsname) == 0)
			break;
	endmntent (f);
	if (!mnt)
		return;

	errx(MKFS_ERROR, _("%s is mounted; will not make a filesystem here!"),
			device_name);
}

static void write_tables(void) {
	unsigned long imaps = get_nimaps();
	unsigned long zmaps = get_nzmaps();
	unsigned long buffsz = get_inode_buffer_size();

	if (lseek(DEV, 0, SEEK_SET))
		err(MKFS_ERROR, _("%s: seek to boot block failed "
				   " in write_tables"), device_name);         
	if (write_all(DEV, boot_block_buffer, 512))
		err(MKFS_ERROR, _("%s: unable to clear boot sector"), device_name);
    	if (EPISODE_BLOCK_SIZE != lseek(DEV, EPISODE_BLOCK_SIZE, SEEK_SET))
		err(MKFS_ERROR, _("%s: seek failed in write_tables"), device_name);
	if (write_all(DEV, super_block_buffer, EPISODE_BLOCK_SIZE))
		err(MKFS_ERROR, _("%s: unable to write super-block"), device_name);
	if (write_all(DEV, inode_map, imaps * EPISODE_BLOCK_SIZE))
		err(MKFS_ERROR, _("%s: unable to write inode map"), device_name);
	if (write_all(DEV, zone_map, zmaps * EPISODE_BLOCK_SIZE))
		err(MKFS_ERROR, _("%s: unable to write zone map"), device_name);
	if (write_all(DEV, inode_buffer, buffsz))
		err(MKFS_ERROR, _("%s: unable to write inodes"), device_name);
}

static void write_block(int blk, char * buffer) {
	if (blk*EPISODE_BLOCK_SIZE != lseek(DEV, blk*EPISODE_BLOCK_SIZE, SEEK_SET))
		errx(MKFS_ERROR, _("%s: seek failed in write_block"), device_name);

	if (write_all(DEV, buffer, EPISODE_BLOCK_SIZE))
		errx(MKFS_ERROR, _("%s: write failed in write_block"), device_name);
}

static void mark_good_blocks(void) {
	int blk;

	for (blk=0 ; blk < used_good_blocks ; blk++)
		mark_zone(good_blocks_table[blk]);
}

static inline int next(unsigned long zone) {
	unsigned long zones = get_nzones();
	unsigned long first_zone = get_first_zone();

	if (!zone)
		zone = first_zone-1;
	while (++zone < zones)
		if (zone_in_use(zone))
			return zone;
	return 0;
}

static int get_free_block(void) {
	unsigned int blk;
	unsigned int zones = get_nzones();
	unsigned int first_zone = get_first_zone();

	if (used_good_blocks+1 >= MAX_GOOD_BLOCKS)
		errx(MKFS_ERROR, _("%s: too many bad blocks"), device_name);
	if (used_good_blocks)
		blk = good_blocks_table[used_good_blocks-1]+1;
	else
		blk = first_zone;
	while (blk < zones && zone_in_use(blk))
		blk++;
	if (blk >= zones)
		errx(MKFS_ERROR, _("%s: not enough good blocks"), device_name);
	good_blocks_table[used_good_blocks] = blk;
	used_good_blocks++;
	return blk;
}

static void make_root_inode_episode(void) {
    	struct episode_inode *inode = &Inode[EPISODE_ROOT_INO];

	mark_inode (EPISODE_ROOT_INO);
	inode->i_zone[0] = get_free_block();
	inode->i_nlinks = 2;
	inode->i_atime = inode->i_mtime = inode->i_ctime = time(NULL);
	root_block[2 * dirsize] = '\0';
	inode->i_size = 2 * dirsize;

	inode->i_mode = S_IFDIR + 0755;
	inode->i_uid = getuid();
	if (inode->i_uid)
		inode->i_gid = getgid();
	write_block(inode->i_zone[0], root_block);
}

static void make_root_inode(void)
{
	return make_root_inode_episode();
}

static void super_set_nzones(void)
{
	Super.s_zones = BLOCKS;
		
}

static void super_init_maxsize(void)
{
	Super.s_max_size = 4096*2147483647UL;

}

static void super_set_map_blocks(unsigned long inodes)
{
       
	Super.s_imap_blocks = UPPER(inodes + 1, BITS_PER_BLOCK);
 
	Super.s_zmap_blocks = UPPER(BLOCKS - (1+get_nimaps()+inode_blocks()),
					     BITS_PER_BLOCK+1);
	Super.s_firstdatazone = first_zone_data();
}

static void super_set_magic(void)
{
	
	Super.s_magic = magic;
}

static void setup_tables(void) {
	unsigned long inodes, zmaps, imaps, zones, i;
  
	super_block_buffer = calloc(1, EPISODE_BLOCK_SIZE);
	if (!super_block_buffer)
		err(MKFS_ERROR, _("%s: unable to alloc buffer for superblock"),
				device_name);

	memset(boot_block_buffer,0,512);
	super_set_magic();
	Super.s_log_zone_size = 0;
	//Super.s_blocksize = BLOCKS;
	Super.s_blocksize = EPISODE_BLOCK_SIZE;

	super_init_maxsize();
	super_set_nzones();
	zones = get_nzones();

	if ( req_nr_inodes == 0 ) 
		inodes = BLOCKS/3;
	else
		inodes = req_nr_inodes;

		inodes = ((inodes + EPISODE_INODES_PER_BLOCK - 1) &
			  ~(EPISODE_INODES_PER_BLOCK - 1));


	Super.s_ninodes = inodes;

	super_set_map_blocks(inodes);
   
	imaps = get_nimaps();

	zmaps = get_nzmaps();

	inode_map = malloc(imaps * EPISODE_BLOCK_SIZE);
	zone_map = malloc(zmaps * EPISODE_BLOCK_SIZE);
	if (!inode_map || !zone_map)
		err(MKFS_ERROR, _("%s: unable to allocate buffers for maps"),
				device_name);
     
	memset(inode_map,0xff,imaps * EPISODE_BLOCK_SIZE);
	memset(zone_map,0xff,zmaps * EPISODE_BLOCK_SIZE);
   
	for (i = get_first_zone() ; i<zones ; i++)
		unmark_zone(i);
	for (i = EPISODE_ROOT_INO ; i<=inodes; i++)
		unmark_inode(i);
	inode_buffer = malloc(get_inode_buffer_size());
	if (!inode_buffer)
		err(MKFS_ERROR, _("%s: unable to allocate buffer for inodes"),
				device_name);

	memset(inode_buffer,0, get_inode_buffer_size());
	printf(_("%lu inodes\n"), inodes);
	printf(_("%lu blocks\n"), zones);
	printf(_("Firstdatazone=%ld (%ld)\n"), get_first_zone(), first_zone_data());
	printf(_("Blocksize=%d\n"), EPISODE_BLOCK_SIZE);
	printf(_("Zonesize=%d\n"), EPISODE_BLOCK_SIZE<<get_zone_size());
	printf(_("Maxsize=%ld\n\n"), get_max_size());


	//struct episode_super_block
        printf(_("s_ninodes=%d\n"), Super.s_ninodes);
	printf(_("s_pad0=%d\n"), Super.s_pad0);
	printf(_("s_imap_blocks=%d\n"), Super.s_imap_blocks);
	printf(_("s_zmap_blocks=%d\n"), Super.s_zmap_blocks);
        printf(_("s_firstdatazone=%d\n"), Super.s_firstdatazone);
	printf(_("s_log_zone_size=%d\n"), Super.s_log_zone_size);
	printf(_("s_pad1=%d\n"), Super.s_pad1);
	printf(_("s_max_size=%llu\n"), Super.s_max_size);
	printf(_("s_zones=%d\n"), Super.s_zones);
	printf(_("s_magic=%u\n"), Super.s_magic);
	printf(_("s_pad2=%d\n"), Super.s_pad2);
	printf(_("s_blocksize=%d\n"), Super.s_blocksize);
	printf(_("s_disk_version=%hhu\n"), Super.s_disk_version);
}

int main(int argc, char ** argv) {
	char * tmp;
	struct stat statbuf;
	char * p;

	if (argc && *argv)
		program_name = *argv;
	if ((p = strrchr(program_name, '/')) != NULL)
		program_name = p+1;
  
	if (argc > 0 && !device_name) {
		device_name = argv[1];
	}

	if (!device_name) {
		usage();
	}
	check_mount();		/* is it already mounted? */
	tmp = root_block;
	*(int *)tmp = 1;
	strcpy(tmp+4,".");
	tmp += dirsize;
	*(int *)tmp = 1;
	strcpy(tmp+4,"..");

	if (stat(device_name, &statbuf) < 0)
		err(MKFS_ERROR, _("%s: stat failed"), device_name);

	if (S_ISBLK(statbuf.st_mode))
		DEV = open(device_name,O_RDWR | O_EXCL);
	else
		errx(MKFS_ERROR, _("now we just support blk dev, will not try to make filesystem on '%s'"), device_name);

	if (DEV<0)
		err(MKFS_ERROR, _("%s: open failed"), device_name);

	int sectorsize;

	if (blkdev_get_sector_size(DEV, &sectorsize) == -1)
		sectorsize = DEFAULT_SECTOR_SIZE;		/* kernel < 2.3.3 */

	if (blkdev_is_misaligned(DEV))
		warnx(_("%s: device is misaligned"), device_name);

	if (EPISODE_BLOCK_SIZE < sectorsize)
		errx(MKFS_ERROR, _("block size smaller than physical "
				"sector size of %s"), device_name);
	if (blkdev_get_size(DEV, &BLOCKS) == -1)
		errx(MKFS_ERROR, _("cannot determine size of %s"),
			device_name);
	BLOCKS /= EPISODE_BLOCK_SIZE;

	if (BLOCKS < 10)
		errx(MKFS_ERROR, _("%s: number of blocks too small"), device_name);

	magic = EPISODE_SUPER_MAGIC;

	setup_tables();

	make_root_inode();
	
	mark_good_blocks();
	write_tables();
	close(DEV);

	return 0;
}
