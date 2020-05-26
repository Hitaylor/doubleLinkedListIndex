/*
 * @Author: 金松昌
 * @Date: 2020-04-20 09:56:32
 * @LastEditors: 金松昌
 * @LastEditTime: 2020-04-21 11:02:43
 * @Description: 
 * 
 */

//（1)用户态unistd.h中定义的ssize_t write(int fd, const void * buf, size_t cont)
//    -->(2)include/linux/syscalls.h中定义的内核的sys_write()
//      -->(3)具体sys_write（）的实现定义在fs/read_write.c,具体为SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *buf ,  size_t, count) 
//        -->(4)fs/read_write.c中的vfs_write(file, buf , count, &pos);
//          -->(5)file.c中设定的fie->f_op->write绑定的函数episode_direct_write()
//            -->(6)file.c中绑定实现的episode_direct_write()
//              -->(7.1)file.c中init_kiocb()实现kiocb和要操作的文件对应的结构体的绑定
//              -->(7.2)file.c中generic_file_write_iter(&kiocb, &iter)
//					-->(7.2.1)generic_write_checks()校验、检查权限，是否超出文件最大长度等
//					-->(7.2.2)__generic_file_write_iter()实现数据的写入操作
//						-->(7.2.2.1)generic_file_direct_write():里面还有2个其他函数：将缓存中的页刷新到磁盘以及将缓存中对应的页置失效的函数，此处忽略
//							-->(7.2.2.1.1) episode_direct_IO()调用了内存自带的blockde的写函数
//								-->(7.2.2.1.1.1)blockdev_direct_IO（）公共函数，同时传入episode_get_block函数
//									-->（7.2.2.1.1.1.1）__blockdev_direct_IO()公共函数
//								
//						-->(7.2.2.2)generic_perform_write,在前面写入的字节数不正确的情况下，执行本函数，
//						-->(7.2.2.3)filemap_write_and_wait_range()确保数据进到磁盘
//					-->(7.2.3)generic_write_sync()


//(2)sys_write()的声明
/*asmlinkage long sys_write(unsigned int fd, const char __user *buf,
			  size_t count);*/

//(3)SYSCALL_DEFINE3()定义的write函数
/*SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos = file_pos_read(f.file);//获取file结构体中的f_pos，也就是当前fd中的读写游标
		ret = vfs_write(f.file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(f.file, pos);
		fdput_pos(f);
	}

	return ret;
}*/

//（4）__vfs_write
/*ssize_t __vfs_write(struct file *file, const char __user *p, size_t count,
		    loff_t *pos)
{
	if (file->f_op->write)//如果file->f_op中绑定了.write，则调用绑定的函数
		return file->f_op->write(file, p, count, pos);
	else if (file->f_op->write_iter)//如果绑定了write_iter则调动绑定的函数
		return new_sync_write(file, p, count, pos);//调用new_sync_write函数
	else//出错
		return -EINVAL;
}*/

//(5)file->f_op绑定的函数结构体
const struct  episode_file_operations = {
    .llseek	= generic_file_llseek,
    .read_iter	= generic_file_read_iter,
    .write	= episode_direct_write,//已经绑定了.write
    .mmap	= generic_file_mmap,
    .fsync		= generic_file_fsync,
    .splice_read	= generic_file_splice_read,
};

//(6)episode_direct_write()
static ssize_t episode_direct_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_kiocb(&kiocb, filp); //(7.1)
	kiocb.ki_pos = *ppos;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = generic_file_write_iter(&kiocb, &iter);//(7.2)

        BUG_ON(ret == -EIOCBQUEUED);
        if (ret > 0)
                *ppos = kiocb.ki_pos;
        return ret;
}

//(7.1)init_kiocb() 绑定kiocb和要写入的文件对应的结构体
static inline void init_kiocb(struct kiocb *kiocb, struct file *filp)
{
        *kiocb = (struct kiocb) {
                .ki_filp = filp,
                .ki_flags = set_iocb_flags(filp),//(7.1)
                .ki_hint = write_hint(filp),//(7.2)
        };
}

//(7.1.1) 设置ki_flags
static inline int set_iocb_flags(struct file *file)
{
    int res = 0;
	res |= IOCB_APPEND;
	res |= IOCB_DIRECT;
	res |= IOCB_SYNC;
    return res;
}

//(7.1.2)获取文件的write_hint，就是更新频率
static inline enum rw_hint write_hint(struct file *file)
{
        if (file->f_write_hint != WRITE_LIFE_NOT_SET)
                return file->f_write_hint;

        return file_inode(file)->i_write_hint;
}

//参考https://blog.csdn.net/qq_32473685/article/details/103494398
//(7.2)
/**
 * generic_file_write_iter - write data to a file
 * @iocb:	IO state structure
 * @from:	iov_iter with data to write
 *
 * This is a wrapper around __generic_file_write_iter() to be used by most
 * filesystems. It takes care of syncing the file in case of O_SYNC file
 * and acquires i_mutex as needed.
 */
/*
ssize_t generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;//出现了inode的结构
	ssize_t ret;

	inode_lock(inode);
    //在检查iocb的时候如果发现带有O_APPEND标志就将offset设置为文件的大小。而这整个过程都是在加锁的情况下完成的，所以带有O_APPEND标志的情况下，文件的写入是原子的，多线程写文件是不会导致数据错乱的。
	ret = generic_write_checks(iocb, from);//(7.2.1)
	if (ret > 0)
		ret = __generic_file_write_iter(iocb, from);//(7.2.2)
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);//(7.2.3)//这里的ret大于零表示有成功提交bio，当然并一定全部提交
	return ret;
}*/

//(7.2.1)
/*
 * Performs necessary checks before doing a write
 *
 * Can adjust writing position or amount of bytes to write.
 * Returns appropriate error code that caller should return or
 * zero in case that write should be allowed.
 */
//写数据的时候，使用正常的写入，但是在写入index的时候，要把appendflag去掉，也就是修改iocb里面的ki_flags，删掉append
/*
inline ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	//获取进程可以创建的文件的最大长度，对应shell中的ulimit命令
    unsigned long limit = rlimit(RLIMIT_FSIZE);//在Linux下的进程资源的限制（struct rlimit），这个结构体在./include/uapi/linux/resource.h中定义
    //为了保证index可以放到4T之后的空间，我们需要解除它对文件大小的限制.https://wenku.baidu.com/view/8149aae21711cc7931b716ff.html

	loff_t pos;

	if (!iov_iter_count(from))
		return 0;

	// FIXME: this is for backwards compatibility with 2.4 
	if (iocb->ki_flags & IOCB_APPEND)
		iocb->ki_pos = i_size_read(inode);//如果是追加模式打开的文件，则修改iocb中的ki_pos变量为文件当前长度。如果我们要用index和izone拼接的话，这里就会有问题，文件大小如果和原来是一样的话，那么我们将来可能不会加append标志
    //如果是写文件的数据部分，我们可以照样使用，但是如果写index部分，则不能使用这部分代码
	pos = iocb->ki_pos;//获取要写入的位置

	if ((iocb->ki_flags & IOCB_NOWAIT) && !(iocb->ki_flags & IOCB_DIRECT))//如果设置了iocb_nowait，且灭有设置IOCB_DIRECT则会出错
		return -EINVAL;
    //https://wenku.baidu.com/view/8149aae21711cc7931b716ff.html
	if (limit != RLIM_INFINITY) {//RLIM_INFINITY表示不对资源限制，默认应该是不限定
		if (iocb->ki_pos >= limit) {//如果要写入的位置，超出了limit，则报错返回。对于我们使用4T之后空间存储index的方案，文件系统中应当设定资源不受限才行
			send_sig(SIGXFSZ, current, 0);
			return -EFBIG;
		}
		iov_iter_truncate(from, limit - (unsigned long)pos);//如果要写入的数据+当前写入位置超出了前面获得的limit，则进行截断，猜测应该是对数据区iovec进行截断
        //./include/linux/uio.h中定义，修改from中的count为第二参数的值
	}
	
    // LFS rule
    //
	//open的是加上O_LARGEFILE标志,MAX_NON_LFS定义为((1UL<<31) - 1)，表示2GB
	if (unlikely(pos + iov_iter_count(from) > MAX_NON_LFS &&
				!(file->f_flags & O_LARGEFILE))) {
		if (pos >= MAX_NON_LFS)
			return -EFBIG;
		iov_iter_truncate(from, MAX_NON_LFS - (unsigned long)pos);
	}

	 // Are we about to exceed the fs block limit ?
	 
	 // If we have written data it becomes a short write.  If we have
	 // exceeded without writing data we send a signal and return EFBIG.
	 // Linus frestrict idea will clean these up nicely..
	
	if (unlikely(pos >= inode->i_sb->s_maxbytes))
		return -EFBIG;

	iov_iter_truncate(from, inode->i_sb->s_maxbytes - pos);
	return iov_iter_count(from);
}*/

//(7.2.2)
/**
 * __generic_file_write_iter - write data to a file
 * @iocb:	IO state structure (file, offset, etc.)
 * @from:	iov_iter with data to write
 *
 * This function does all the work needed for actually writing data to a
 * file. It does all basic checks, removes SUID from the file, updates
 * modification times and calls proper subroutines depending on whether we
 * do direct IO or a standard buffered write.
 *
 * It expects i_mutex to be grabbed unless we work on a block device or similar
 * object which does not need locking at all.
 *
 * This function does *not* take care of syncing data in case of O_SYNC write.
 * A caller has to handle it. This is mainly due to the fact that we want to
 * avoid syncing under i_mutex.
 */
/*
ssize_t __generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
	struct inode 	*inode = mapping->host;
	ssize_t		written = 0;
	ssize_t		err;
	ssize_t		status;

	// We can write back this queue in page reclaim 
	current->backing_dev_info = inode_to_bdi(inode);
	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	if (iocb->ki_flags & IOCB_DIRECT) {//此条件成立
		loff_t pos, endbyte;

		written = generic_file_direct_write(iocb, from);//(7.2.2.1)
		///*
		 //* If the write stopped short of completing, fall back to
		 //* buffered writes.  Some filesystems do this for writes to
		 //* holes, for example.  For DAX files, a buffered write will
		// * not succeed (even if it did, DAX does not handle dirty
		// * page-cache pages correctly).
		 
		if (written < 0 || !iov_iter_count(from) || IS_DAX(inode))//如果写入的字节和iov_iter中的count一致，说明写入成功，就可以返回了。下面代码不用执行
			goto out;

		status = generic_perform_write(file, from, pos = iocb->ki_pos);//(7.2.2.2)
		//*
		// * If generic_perform_write() returned a synchronous error
		 //* then we want to return the number of bytes which were
		// * direct-written, or the error code if that was zero.  Note
		// * that this differs from normal direct-io semantics, which
		// * will return -EFOO even if some bytes were written.
		// 
		if (unlikely(status < 0)) {
			err = status;
			goto out;
		}
		
		//* We need to ensure that the page cache pages are written to
		// * disk and invalidated to preserve the expected O_DIRECT
		// * semantics.
		// 
		endbyte = pos + status - 1;
		err = filemap_write_and_wait_range(mapping, pos, endbyte);//(7.2.2.3)
		if (err == 0) {
			iocb->ki_pos = endbyte + 1;
			written += status;
			invalidate_mapping_pages(mapping,
						 pos >> PAGE_SHIFT,
						 endbyte >> PAGE_SHIFT);//(7.2.2.4)
		} else {
		}
	} else {//不执行
		written = generic_perform_write(file, from, iocb->ki_pos);//缓存写
		if (likely(written > 0))
			iocb->ki_pos += written;
	}
out:
	current->backing_dev_info = NULL;
	return written ? written : err;
}
*/

//(7.2.3)
/*
 * Sync the bytes written if this was a synchronous write.  Expect ki_pos
 * to already be updated for the write, and will return either the amount
 * of bytes passed in, or an error if syncing the file failed.
 */
/*static inline ssize_t generic_write_sync(struct kiocb *iocb, ssize_t count)
{
	if (iocb->ki_flags & IOCB_DSYNC) {
		int ret = vfs_fsync_range(iocb->ki_filp,
				iocb->ki_pos - count, iocb->ki_pos - 1,
				(iocb->ki_flags & IOCB_SYNC) ? 0 : 1); //(7.2.3.1)
		if (ret)
			return ret;
	}

	return count;
}*/

//(7.2.3.1)
/**
 * vfs_fsync_range - helper to sync a range of data & metadata to disk
 * @file:		file to sync
 * @start:		offset in bytes of the beginning of data range to sync
 * @end:		offset in bytes of the end of data range (inclusive)
 * @datasync:		perform only datasync
 *
 * Write back data in range @start..@end and metadata for @file to disk.  If
 * @datasync is set only metadata needed to access modified file data is
 * written.
 */
/*int vfs_fsync_range(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file->f_mapping->host;

	if (!file->f_op->fsync)
		return -EINVAL;
	if (!datasync && (inode->i_state & I_DIRTY_TIME)) {
		spin_lock(&inode->i_lock);
		inode->i_state &= ~I_DIRTY_TIME;
		spin_unlock(&inode->i_lock);
		mark_inode_dirty_sync(inode);
	}
	return file->f_op->fsync(file, start, end, datasync);//这里绑定的是generic_file_fsync（）属于内核提供的(7.2.3.1.1)
}*/

//(7.2.2.1)generic_file_direct_write（）
/*
ssize_t generic_file_direct_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file	*file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode	*inode = mapping->host;
	loff_t		pos = iocb->ki_pos;
	ssize_t		written;
	size_t		write_len;
	pgoff_t		end;

	write_len = iov_iter_count(from);//获取待写入的长度
	end = (pos + write_len - 1) >> PAGE_SHIFT;//计算写入后的page num

	if (iocb->ki_flags & IOCB_NOWAIT) {
		// If there are pages to writeback, return 
		if (filemap_range_has_page(inode->i_mapping, pos,
					   pos + iov_iter_count(from)))
			return -EAGAIN;
	} else {
		written = filemap_write_and_wait_range(mapping, pos,
							pos + write_len - 1);//对目的区域的缓存进行刷写,也就是在direct write之前，先把缓存中的脏数据，写入磁盘，然后再direct write，防止顺序反了的情况下，缓存中的脏数据会把direct write到磁盘的数据再次冲掉
		if (written)
			goto out;
	}

	
	 //* After a write we want buffered reads to be sure to go to disk to get
	 //* the new data.  We invalidate clean cached page from the region we're
	 //* about to write.  We do this *before* the write so that we can return
	 //* without clobbering -EIOCBQUEUED from ->direct_IO().
	 
	written = invalidate_inode_pages2_range(mapping,
					pos >> PAGE_SHIFT, end);//并使缓存中从pos>>Page_SHIFT到end的页失效，防止有人从缓存中读到错误的数据。这里就要求用户读缓存时，没有这些页的缓存，需要从磁盘获取最新的数据放到缓存。
					//为什么不直接放缓存呢？因为用的direct write，绕过了缓存，所以只能使历史缓存失效，然后再重新从磁盘获取
	
	// * If a page can not be invalidated, return 0 to fall back
	// * to buffered write.
	
	if (written) {
		if (written == -EBUSY)
			return 0;
		goto out;
	}

	written = mapping->a_ops->direct_IO(iocb, from);//调用我们字节的direct_IO函数episode_direct_IO（）
	//(7.2.2.1.1)


	if (mapping->nrpages)
		invalidate_inode_pages2_range(mapping,
					pos >> PAGE_SHIFT, end);//再清一遍缓存，防止在directIO的同时，有人使用非directIO对缓存进行写入。如果是都使用缓存，则由于锁的存在，缓存数据不会混乱
					//但现在我们用direct io，不会对缓存中对应页加锁，而其他非directio在对缓存操作时，可能加锁，但两边并不冲突。所以这种情况下，有必要再次清一下缓存，保证我们direct io的正确性

	if (written > 0) {
		pos += written;
		write_len -= written;
		if (pos > i_size_read(inode) && !S_ISBLK(inode->i_mode)) {
			i_size_write(inode, pos);//更新inode的isize字段
			mark_inode_dirty(inode);//标记inode已经修改
		}
		iocb->ki_pos = pos;//更新控制块中的ki_pos字段
	}
	iov_iter_revert(from, write_len - iov_iter_count(from));
out:
	return written;
}
*/


//(7.2.2.1.1) episode_direct_IO（）
static ssize_t episode_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
        struct file *file = iocb->ki_filp;
        struct address_space *mapping = file->f_mapping;
        struct inode *inode = mapping->host;
        size_t count = iov_iter_count(iter);
        loff_t offset = iocb->ki_pos;
        ssize_t ret;

        if (WARN_ON_ONCE(IS_DAX(inode)))
                return -EIO;

        ret = blockdev_direct_IO(iocb, inode, iter, episode_get_block);//(7.2.2.1.1.1)内核自带的通用函数,同时依赖episode_get_block
	//这里是根据iocb和iter中的数据，根据inode中的isize信息，确定目标blockid，并使用episode_get_block函数去获取blockid
        if (ret < 0 && iov_iter_rw(iter) == WRITE)
                episode_write_failed(mapping, offset + count);//写失败的处理函数（7.2.2.1.1.2)
        return ret;
}

//写失败的处理函数（7.2.2.1.1.2)
void episode_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);//（7.2.2.1.1.2.1)
		episode_truncate(inode);//（7.2.2.1.1.2.2)
	}
}
//（7.2.2.1.1.2.2) http://www.voidcn.com/article/p-cxmtqxni-sh.html
void episode_truncate(struct inode * inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;
	if (INODE_VERSION(inode) == EPISODE_V)
		truncate(inode);//（7.2.2.1.1.2.2.1)
}
//（7.2.2.1.1.2.2.1) 释放文件占用的磁盘块
static inline void truncate (struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	block_t *idata = i_data(inode);
	int offsets[DEPTH];
	Indirect chain[DEPTH];
	Indirect *partial;
	block_t nr = 0;
	int n;
	int first_whole;
	long iblock;

	iblock = (inode->i_size + sb->s_blocksize -1) >> sb->s_blocksize_bits;//文件占了多少个block，包括空洞
	block_truncate_page(inode->i_mapping, inode->i_size, get_block);//（7.2.2.1.1.2.2.1.1)

	n = block_to_path(inode, iblock, offsets);//找到最后一个block对应的索引位置，其实就是确定它用到了几级索引
	if (!n)
		return;

	if (n == 1) {//只有直接索引，那直接释放就行了
		free_data(inode, idata+offsets[0], idata + DIRECT);
		first_whole = 0;
		goto do_indirects;
	}

	first_whole = offsets[0] + 1 - DIRECT;
	partial = find_shared(inode, n, offsets, chain, &nr);
	if (nr) {
		if (partial == chain)
			mark_inode_dirty(inode);
		else
			mark_buffer_dirty_inode(partial->bh, inode);
		free_branches(inode, &nr, &nr+1, (chain+n-1) - partial);
	}
	/* Clear the ends of indirect blocks on the shared branch */
	while (partial > chain) {
		free_branches(inode, partial->p + 1, block_end(partial->bh),
				(chain+n-1) - partial);
		mark_buffer_dirty_inode(partial->bh, inode);
		brelse (partial->bh);
		partial--;
	}
do_indirects:
	/* Kill the remaining (whole) subtrees */
	while (first_whole < DEPTH-1) {
		nr = idata[DIRECT+first_whole];
		if (nr) {
			idata[DIRECT+first_whole] = 0;
			mark_inode_dirty(inode);
			free_branches(inode, &nr, &nr+1, first_whole+1);
		}
		first_whole++;
	}
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);
}

//
int block_truncate_page(struct address_space *mapping,
			loff_t from, get_block_t *get_block)
{
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (PAGE_SIZE-1);
	unsigned blocksize;
	sector_t iblock;
	unsigned length, pos;
	struct inode *inode = mapping->host;
	struct page *page;
	struct buffer_head *bh;
	int err;

	blocksize = i_blocksize(inode);
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;
	iblock = (sector_t)index << (PAGE_SHIFT - inode->i_blkbits);
	
	page = grab_cache_page(mapping, index);
	err = -ENOMEM;
	if (!page)
		goto out;

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	/* Find the buffer that contains "offset" */
	bh = page_buffers(page);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	err = 0;
	if (!buffer_mapped(bh)) {
		WARN_ON(bh->b_size != blocksize);
		err = get_block(inode, iblock, bh, 0);
		if (err)
			goto unlock;
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh))
			goto unlock;
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (PageUptodate(page))
		set_buffer_uptodate(bh);

	if (!buffer_uptodate(bh) && !buffer_delay(bh) && !buffer_unwritten(bh)) {
		err = -EIO;
		ll_rw_block(REQ_OP_READ, 0, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
	}

	zero_user(page, offset, length);
	mark_buffer_dirty(bh);
	err = 0;

unlock:
	unlock_page(page);
	put_page(page);
out:
	return err;
}

//(7.2.2.1.1.1) blockdev_direct_IO
/*static inline ssize_t blockdev_direct_IO(struct kiocb *iocb,
					 struct inode *inode,
					 struct iov_iter *iter,
				 get_block_t get_block)
{
	return __blockdev_direct_IO(iocb, inode, inode->i_sb->s_bdev, iter,
		get_block, NULL, NULL, DIO_LOCKING | DIO_SKIP_HOLES);//(7.2.2.1.1.1.1) 
}*/

//(7.2.2.1.1.1.1) __blockdev_direct_IO
/*ssize_t __blockdev_direct_IO(struct kiocb *iocb, struct inode *inode,
			     struct block_device *bdev, struct iov_iter *iter,
			     get_block_t get_block,
			     dio_iodone_t end_io, dio_submit_t submit_io,
			     int flags)
{
	
	// * The block device state is needed in the end to finally
	// * submit everything.  Since it's likely to be cache cold
	// * prefetch it here as first thing to hide some of the
	 //* latency.
	// *
	// * Attempt to prefetch the pieces we likely need later.
	
	prefetch(&bdev->bd_disk->part_tbl);
	prefetch(bdev->bd_queue);
	prefetch((char *)bdev->bd_queue + SMP_CACHE_BYTES);

	return do_blockdev_direct_IO(iocb, inode, bdev, iter, get_block,
				     end_io, submit_io, flags);//(7.2.2.1.1.1.1.1)
}*/
//(7.2.2.1.1.1.1.1) do_blockdev_direct_IO
/*
 * This is a library function for use by filesystem drivers.
 *
 * The locking rules are governed by the flags parameter:
 *  - if the flags value contains DIO_LOCKING we use a fancy locking
 *    scheme for dumb filesystems.
 *    For writes this function is called under i_mutex and returns with
 *    i_mutex held, for reads, i_mutex is not held on entry, but it is
 *    taken and dropped again before returning.
 *  - if the flags value does NOT contain DIO_LOCKING we don't use any
 *    internal locking but rather rely on the filesystem to synchronize
 *    direct I/O reads/writes versus each other and truncate.
 *
 * To help with locking against truncate we incremented the i_dio_count
 * counter before starting direct I/O, and decrement it once we are done.
 * Truncate can wait for it to reach zero to provide exclusion.  It is
 * expected that filesystem provide exclusion between new direct I/O
 * and truncates.  For DIO_LOCKING filesystems this is done by i_mutex,
 * but other filesystems need to take care of this on their own.
 *
 * NOTE: if you pass "sdio" to anything by pointer make sure that function
 * is always inlined. Otherwise gcc is unable to split the structure into
 * individual fields and will generate much worse code. This is important
 * for the whole file.
 */
static inline ssize_t
do_blockdev_direct_IO(struct kiocb *iocb, struct inode *inode,
		      struct block_device *bdev, struct iov_iter *iter,
		      get_block_t get_block, dio_iodone_t end_io,
		      dio_submit_t submit_io, int flags)
{
	unsigned i_blkbits = READ_ONCE(inode->i_blkbits);
	unsigned blkbits = i_blkbits;
	unsigned blocksize_mask = (1 << blkbits) - 1;
	ssize_t retval = -EINVAL;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	loff_t end = offset + count;
	struct dio *dio;
	struct dio_submit sdio = { 0, };
	struct buffer_head map_bh = { 0, };
	struct blk_plug plug;
	unsigned long align = offset | iov_iter_alignment(iter);

	
	//读写文件的起始地址是否对齐到inode block或至少对齐到块设备的block大小,/
	if (align & blocksize_mask) {//对齐检查
		if (bdev)
			blkbits = blksize_bits(bdev_logical_block_size(bdev));
		blocksize_mask = (1 << blkbits) - 1;
		if (align & blocksize_mask)
			goto out;
	}

	/* watch out for a 0 len io from a tricksy fs */
	if (iov_iter_rw(iter) == READ && !iov_iter_count(iter))
		return 0;

	dio = kmem_cache_alloc(dio_cache, GFP_KERNEL);
	retval = -ENOMEM;
	if (!dio)
		goto out;
	
	memset(dio, 0, offsetof(struct dio, pages));

	dio->flags = flags;
	if (dio->flags & DIO_LOCKING) {//direct IO里设了这个标记，所以会执行一下流程
		if (iov_iter_rw(iter) == READ) {//这里不会执行
			struct address_space *mapping =
					iocb->ki_filp->f_mapping;

			/* will be released by direct_io_worker */
			inode_lock(inode);

			retval = filemap_write_and_wait_range(mapping, offset,
							      end - 1);//这里是将文件对应偏移位置的缓存先刷下磁盘，原因很好理解，write已经在外层执行过了，但这里是read
			if (retval) {
				inode_unlock(inode);
				kmem_cache_free(dio_cache, dio);
				goto out;
			}
		}
	}

		//这里是读到文件末尾以后，上面那个是读长度为0
	dio->i_size = i_size_read(inode);
	if (iov_iter_rw(iter) == READ && offset >= dio->i_size) {//不会执行
		if (dio->flags & DIO_LOCKING)
			inode_unlock(inode);
		kmem_cache_free(dio_cache, dio);
		retval = 0;
		goto out;
	}

	
	 //* For file extending writes updating i_size before data writeouts
	// * complete can expose uninitialized blocks in dumb filesystems.
	// * In that case we need to wait for I/O completion even if asked
	 //* for an asynchronous write.
	 
	if (is_sync_kiocb(iocb))//这里判断成立,前面已经对iocb初始化过了
		dio->is_async = false;
	else if (!(dio->flags & DIO_ASYNC_EXTEND) &&
		 iov_iter_rw(iter) == WRITE && end > i_size_read(inode))
		dio->is_async = false;
	else
		dio->is_async = true;

	dio->inode = inode;
	if (iov_iter_rw(iter) == WRITE) {//执行这一个
		dio->op = REQ_OP_WRITE;
		dio->op_flags = REQ_SYNC | REQ_IDLE;
		if (iocb->ki_flags & IOCB_NOWAIT)
			dio->op_flags |= REQ_NOWAIT;
	} else {
		dio->op = REQ_OP_READ;
	}

	/*
	 * For AIO O_(D)SYNC writes we need to defer completions to a workqueue
	 * so that we can call ->fsync.
	 */
	if (dio->is_async && iov_iter_rw(iter) == WRITE) {
		retval = 0;
		if (iocb->ki_flags & IOCB_DSYNC)
			retval = dio_set_defer_completion(dio);
		else if (!dio->inode->i_sb->s_dio_done_wq) {
			/*
			 * In case of AIO write racing with buffered read we
			 * need to defer completion. We can't decide this now,
			 * however the workqueue needs to be initialized here.
			 */
			retval = sb_init_dio_done_wq(dio->inode->i_sb);
		}
		if (retval) {
			/*
			 * We grab i_mutex only for reads so we don't have
			 * to release it here
			 */
			kmem_cache_free(dio_cache, dio);
			goto out;
		}
	}

	/*
	 * Will be decremented at I/O completion time.
	 */
	if (!(dio->flags & DIO_SKIP_DIO_COUNT))
		inode_dio_begin(inode);

	retval = 0;
	sdio.blkbits = blkbits;
	sdio.blkfactor = i_blkbits - blkbits;
	sdio.block_in_file = offset >> blkbits;

	sdio.get_block = get_block;
	dio->end_io = end_io;
	sdio.submit_io = submit_io;
	sdio.final_block_in_bio = -1;
	sdio.next_block_for_io = -1;

	dio->iocb = iocb;

	spin_lock_init(&dio->bio_lock);
	dio->refcount = 1;

	dio->should_dirty = (iter->type == ITER_IOVEC);
	sdio.iter = iter;
	sdio.final_block_in_request =
		(offset + iov_iter_count(iter)) >> blkbits;

	/*
	 * In case of non-aligned buffers, we may need 2 more
	 * pages since we need to zero out first and last block.
	 */
	if (unlikely(sdio.blkfactor))
		sdio.pages_in_io = 2;

	sdio.pages_in_io += iov_iter_npages(iter, INT_MAX);

	blk_start_plug(&plug);

	retval = do_direct_IO(dio, &sdio, &map_bh);//(7.2.2.1.1.1.1.1.1)
	if (retval)
		dio_cleanup(dio, &sdio);

	if (retval == -ENOTBLK) {
		/*
		 * The remaining part of the request will be
		 * be handled by buffered I/O when we return
		 */
		retval = 0;
	}
	/*
	 * There may be some unwritten disk at the end of a part-written
	 * fs-block-sized block.  Go zero that now.
	 */
	dio_zero_block(dio, &sdio, 1, &map_bh);

	if (sdio.cur_page) {
		ssize_t ret2;

		ret2 = dio_send_cur_page(dio, &sdio, &map_bh);
		if (retval == 0)
			retval = ret2;
		put_page(sdio.cur_page);
		sdio.cur_page = NULL;
	}
	if (sdio.bio)
		dio_bio_submit(dio, &sdio);

	blk_finish_plug(&plug);

	/*
	 * It is possible that, we return short IO due to end of file.
	 * In that case, we need to release all the pages we got hold on.
	 */
	dio_cleanup(dio, &sdio);

	/*
	 * All block lookups have been performed. For READ requests
	 * we can let i_mutex go now that its achieved its purpose
	 * of protecting us from looking up uninitialized blocks.
	 */
	if (iov_iter_rw(iter) == READ && (dio->flags & DIO_LOCKING))
		inode_unlock(dio->inode);

	/*
	 * The only time we want to leave bios in flight is when a successful
	 * partial aio read or full aio write have been setup.  In that case
	 * bio completion will call aio_complete.  The only time it's safe to
	 * call aio_complete is when we return -EIOCBQUEUED, so we key on that.
	 * This had *better* be the only place that raises -EIOCBQUEUED.
	 */
	BUG_ON(retval == -EIOCBQUEUED);
	if (dio->is_async && retval == 0 && dio->result &&
	    (iov_iter_rw(iter) == READ || dio->result == count))
		retval = -EIOCBQUEUED;
	else
		dio_await_completion(dio);

	if (drop_refcount(dio) == 0) {
		retval = dio_complete(dio, retval, DIO_COMPLETE_INVALIDATE);
	} else
		BUG_ON(retval != -EIOCBQUEUED);

out:
	return retval;
}


//(7.2.2.1.1.1.1.1.1)
/*
 * Walk the user pages, and the file, mapping blocks to disk and generating
 * a sequence of (page,offset,len,block) mappings.  These mappings are injected
 * into submit_page_section(), which takes care of the next stage of submission
 *
 * Direct IO against a blockdev is different from a file.  Because we can
 * happily perform page-sized but 512-byte aligned IOs.  It is important that
 * blockdev IO be able to have fine alignment and large sizes.
 *
 * So what we do is to permit the ->get_block function to populate bh.b_size
 * with the size of IO which is permitted at this offset and this i_blkbits.
 *
 * For best results, the blockdev should be set up with 512-byte i_blkbits and
 * it should set b_size to PAGE_SIZE or more inside get_block().  This gives
 * fine alignment but still allows this function to work in PAGE_SIZE units.
 */
static int do_direct_IO(struct dio *dio, struct dio_submit *sdio,
			struct buffer_head *map_bh)
{
	const unsigned blkbits = sdio->blkbits;
	const unsigned i_blkbits = blkbits + sdio->blkfactor;
	int ret = 0;

	while (sdio->block_in_file < sdio->final_block_in_request) {
		struct page *page;
		size_t from, to;

		page = dio_get_page(dio, sdio);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		from = sdio->head ? 0 : sdio->from;
		to = (sdio->head == sdio->tail - 1) ? sdio->to : PAGE_SIZE;
		sdio->head++;

		while (from < to) {
			unsigned this_chunk_bytes;	/* # of bytes mapped */
			unsigned this_chunk_blocks;	/* # of blocks */
			unsigned u;

			if (sdio->blocks_available == 0) {
				/*
				 * Need to go and map some more disk
				 */
				unsigned long blkmask;
				unsigned long dio_remainder;

				ret = get_more_blocks(dio, sdio, map_bh);
				if (ret) {
					put_page(page);
					goto out;
				}
				if (!buffer_mapped(map_bh))
					goto do_holes;

				sdio->blocks_available =
						map_bh->b_size >> blkbits;
				sdio->next_block_for_io =
					map_bh->b_blocknr << sdio->blkfactor;
				if (buffer_new(map_bh)) {
					clean_bdev_aliases(
						map_bh->b_bdev,
						map_bh->b_blocknr,
						map_bh->b_size >> i_blkbits);
				}

				if (!sdio->blkfactor)
					goto do_holes;

				blkmask = (1 << sdio->blkfactor) - 1;
				dio_remainder = (sdio->block_in_file & blkmask);

				/*
				 * If we are at the start of IO and that IO
				 * starts partway into a fs-block,
				 * dio_remainder will be non-zero.  If the IO
				 * is a read then we can simply advance the IO
				 * cursor to the first block which is to be
				 * read.  But if the IO is a write and the
				 * block was newly allocated we cannot do that;
				 * the start of the fs block must be zeroed out
				 * on-disk
				 */
				if (!buffer_new(map_bh))
					sdio->next_block_for_io += dio_remainder;
				sdio->blocks_available -= dio_remainder;
			}
do_holes:
			/* Handle holes */
			if (!buffer_mapped(map_bh)) {
				loff_t i_size_aligned;

				/* AKPM: eargh, -ENOTBLK is a hack */
				if (dio->op == REQ_OP_WRITE) {
					put_page(page);
					return -ENOTBLK;
				}

				/*
				 * Be sure to account for a partial block as the
				 * last block in the file
				 */
				i_size_aligned = ALIGN(i_size_read(dio->inode),
							1 << blkbits);
				if (sdio->block_in_file >=
						i_size_aligned >> blkbits) {
					/* We hit eof */
					put_page(page);
					goto out;
				}
				zero_user(page, from, 1 << blkbits);
				sdio->block_in_file++;
				from += 1 << blkbits;
				dio->result += 1 << blkbits;
				goto next_block;
			}

			/*
			 * If we're performing IO which has an alignment which
			 * is finer than the underlying fs, go check to see if
			 * we must zero out the start of this block.
			 */
			if (unlikely(sdio->blkfactor && !sdio->start_zero_done))
				dio_zero_block(dio, sdio, 0, map_bh);

			/*
			 * Work out, in this_chunk_blocks, how much disk we
			 * can add to this page
			 */
			this_chunk_blocks = sdio->blocks_available;
			u = (to - from) >> blkbits;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			u = sdio->final_block_in_request - sdio->block_in_file;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			this_chunk_bytes = this_chunk_blocks << blkbits;
			BUG_ON(this_chunk_bytes == 0);

			if (this_chunk_blocks == sdio->blocks_available)
				sdio->boundary = buffer_boundary(map_bh);
			ret = submit_page_section(dio, sdio, page,
						  from,
						  this_chunk_bytes,
						  sdio->next_block_for_io,
						  map_bh);
			if (ret) {
				put_page(page);
				goto out;
			}
			sdio->next_block_for_io += this_chunk_blocks;

			sdio->block_in_file += this_chunk_blocks;
			from += this_chunk_bytes;
			dio->result += this_chunk_bytes;
			sdio->blocks_available -= this_chunk_blocks;
next_block:
			BUG_ON(sdio->block_in_file > sdio->final_block_in_request);
			if (sdio->block_in_file == sdio->final_block_in_request)
				break;
		}

		/* Drop the ref which was taken in get_user_pages() */
		put_page(page);
	}
out:
	return ret;
}




//(7.2.2.1.1.2)
int episode_get_block(struct inode *inode, sector_t block,
		    struct buffer_head *bh_result, int create)
{
	if (INODE_VERSION(inode) == EPISODE_V)
		return __episode_get_block(inode, block, bh_result, create);
	else
		return -1;
}
//(7.2.2.1.1.2.1)
int __episode_get_block(struct inode * inode, long block,
			struct buffer_head *bh_result, int create)
{
	return get_block(inode, block, bh_result, create);
}
//(7.2.2.1.1.2.1.1)========A
//https://blog.csdn.net/yunsongice/article/details/6171186
static int get_block(struct inode * inode, sector_t block,
			struct buffer_head *bh, int create)
{
	int err = -EIO;
	int offsets[DEPTH];
	Indirect chain[DEPTH];
	Indirect *partial;
	int left;
	int depth = block_to_path(inode, block, offsets);//A.1

	if (depth == 0)
		goto out;

reread:
	partial = get_branch(inode, depth, offsets, chain, &err);//A.2

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
got_it:
		map_bh(bh, inode->i_sb, block_to_cpu(chain[depth-1].key));//A.3将bh这个buffer_head的b_bdev、b_blocknr和b_size字段分别设置为该文件超级块的设备，chain[3]的key字段（block_in_file对应的逻辑块号）和块大小1024
		/* Clean up and exit */
		partial = chain+depth-1; /* the whole chain */
		goto cleanup;
	}

	/* Next simple case - plain lookup or failed read of indirect block */
	if (!create || err == -EIO) {
cleanup:
		while (partial > chain) {
			brelse(partial->bh);
			partial--;
		}
out:
		return err;
	}

	/*
	 * Indirect block might be removed by truncate while we were
	 * reading it. Handling of that case (forget what we've got and
	 * reread) is taken out of the main path.
	 */
	if (err == -EAGAIN)
		goto changed;

	left = (chain + depth) - partial;
	err = alloc_branch(inode, left, offsets+(partial-chain), partial);//A.4
	if (err)
		goto cleanup;

	if (splice_branch(inode, chain, partial, left) < 0)//A.5
		goto changed;

	set_buffer_new(bh);
	goto got_it;

changed:
	while (partial > chain) {
		brelse(partial->bh);
		partial--;
	}
	goto reread;
}


//A.1 找到通向块的路径，就是确定该块是直接的还是间接的？是几次间接？每一次间接在间接块中的偏移量是多少？有了这些信息我们就一定可以定位到一个块
//https://blog.csdn.net/zouxiaoting/article/details/8943091
static int block_to_path(struct inode * inode, long block, int offsets[DEPTH])
{
	int n = 0;
	struct super_block *sb = inode->i_sb;

	if (block < 0) {
		printk("episode-fs: block_to_path: block %ld < 0 on dev %pg\n",
			block, sb->s_bdev);
	} else if ((u64)block * (u64)sb->s_blocksize >=
			episode_sb(sb)->s_max_size) {
		if (printk_ratelimit())
			printk("episode-fs: block_to_path: "
			       "block %ld too big on dev %pg\n",
				block, sb->s_bdev);
	} else if (block < DIRCOUNT) {
		offsets[n++] = block;
	} else if ((block -= DIRCOUNT) < INDIRCOUNT(sb)) {
		offsets[n++] = DIRCOUNT;
		offsets[n++] = block;
	} else if ((block -= INDIRCOUNT(sb)) < INDIRCOUNT(sb) * INDIRCOUNT(sb)) {
		offsets[n++] = DIRCOUNT + 1;
		offsets[n++] = block / INDIRCOUNT(sb);
		offsets[n++] = block % INDIRCOUNT(sb);
	} else {
		block -= INDIRCOUNT(sb) * INDIRCOUNT(sb);
		offsets[n++] = DIRCOUNT + 2;
		offsets[n++] = (block / INDIRCOUNT(sb)) / INDIRCOUNT(sb);
		offsets[n++] = (block / INDIRCOUNT(sb)) % INDIRCOUNT(sb);
		offsets[n++] = block % INDIRCOUNT(sb);
	}
	return n;
}
//A.2 get_branch 跟踪路径offsets，最终到达一个数据块
//如果找到了指定的块，就返回null；否则返回一个Indirect对象的指针，表示最后的间接块的地址，后续再进行文件扩展，就基于此开始
//当ext2_get_branch循着路径试图到达一个数据块时，将过程中读取到的间接块信息保存在Indirect类型的数组chain中，每一次填充chain数组中的Indirect实例，
//都会检查key是否为0。如果是，则表示要寻找的数据块不存在，即检测到没有指向下一个层次间接块（或数据块，如果是直接分配）的指针，这时返回当前的这个不完整的
//Indirect实例。这时候p成员指向下一层次间接块或数据块应该出现在间接块中的位置，但key为0，因为该块尚未分配。
static inline Indirect *get_branch(struct inode *inode,
					int depth,
					int *offsets,
					Indirect chain[DEPTH],
					int *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	struct buffer_head *bh;

	*err = 0;
	/* i_data is not going away, no lock needed */
	add_chain(chain, NULL, i_data(inode) + *offsets);
	if (!p->key)
		goto no_block;
	while (--depth) {
		bh = sb_bread(sb, block_to_cpu(p->key));
		if (!bh)
			goto failure;
		read_lock(&pointers_lock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, bh, (block_t *)bh->b_data + *++offsets);
		read_unlock(&pointers_lock);
		if (!p->key)
			goto no_block;
	}
	return NULL;

changed:
	read_unlock(&pointers_lock);
	brelse(bh);
	*err = -EAGAIN;
	goto no_block;
failure:
	*err = -EIO;
no_block:
	return p;
}

//A.3 数据长度类型格式转换
static inline unsigned long block_to_cpu(block_t n)
{
	return n;
}

static inline block_t cpu_to_block(unsigned long n)
{
	return n;
}

//A.3 map_bh(bh, inode->i_sb, block_to_cpu(chain[depth-1].key))
//将bh这个buffer_head的b_bdev、b_blocknr分别设置为该文件超级块的设备，chain[depth-1]的key字段（block_in_file对应的逻辑块号）
/*static inline void
map_bh(struct buffer_head *bh, struct super_block *sb, sector_t block)
{
	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = block;
	bh->b_size = sb->s_blocksize;
}*/

//A.4 alloc_branch,根据需要的磁盘空间的数量num,分配需要的磁盘空间,具体就是将空间管理的位图置位
//对给定的新路径分配所需的块，并建立连接块的间接链，即建立间接块的Indirect实例。所以alloc_blocks一次性把需要分配的块都分配到了。实际上分配块的繁重的任务是由new_blocks完成的，alloc_blocks一直重复的调用它直到完成目的
//一次性分配完所需要的块，这里所谓的一次性分配完，对外来看，其实还是1个block，只不过这里可能用到了多级索引，那个这是的block可能对一个多个间接block
//根据num确定要分配几级block，也就是几个block，然后在缓冲区分别找到或者创建对应的bh，然后根据offsets[]中的值，将找到的blockid填入到bh对应的缓冲块中的对应位置
static int alloc_branch(struct inode *inode,
			     int num,
			     int *offsets,
			     Indirect *branch)
{
	int n = 0;
	int i;
	int parent = episode_new_block(inode);//A4.1，先找一个可用的block，得到blockid给brach[0]

	branch[0].key = cpu_to_block(parent);
	if (parent) 
		for (n = 1; n < num; n++) {
			struct buffer_head *bh;
			/* Allocate the next block */
			int nr = episode_new_block(inode);//A4.1
			if (!nr)
				break;
			branch[n].key = cpu_to_block(nr);
			bh = sb_getblk(inode->i_sb, parent);//A4.2在缓冲区建立一个缓冲头，用于存储parent指向的block的数据
			lock_buffer(bh);
			memset(bh->b_data, 0, bh->b_size);//bh缓冲头对应的缓冲块先清0
			branch[n].bh = bh;//连起来
			branch[n].p = (block_t*) bh->b_data + offsets[n];//找到要填入数据（blockid）的位置
			*branch[n].p = branch[n].key;//将blockid写入
			set_buffer_uptodate(bh);
			unlock_buffer(bh);
			mark_buffer_dirty_inode(bh, inode);
			parent = nr;
		}
	if (n == num)
		return 0;

	/* Allocation failed, free what we already allocated */
	for (i = 1; i < n; i++)
		bforget(branch[i].bh);
	for (i = 0; i < n; i++)
		episode_free_block(inode, block_to_cpu(branch[i].key));//A4.3
	return -ENOSPC;
}

//A4.1 为inode找到一个可用的block
int episode_new_block(struct inode * inode)
{
	struct episode_sb_info *sbi = episode_sb(inode->i_sb);//A4.1.1
	int bits_per_zone = 8 * inode->i_sb->s_blocksize;
	int i;

	for (i = 0; i < sbi->s_zmap_blocks; i++) {//遍历缓存中所有的存储map的block，找到一个为0的bit位
		struct buffer_head *bh = sbi->s_zmap[i];
		int j;

		spin_lock(&bitmap_lock);
		j = episode_find_first_zero_bit(bh->b_data, bits_per_zone);//A4.1.2
		if (j < bits_per_zone) {
			episode_set_bit(j, bh->b_data);//A4.1.3
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
//A4.1.1
static inline struct episode_sb_info *episode_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}
//A4.1.2
#define episode_find_first_zero_bit(addr, size) \
	find_first_zero_bit((unsigned long *)(addr), (size))
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
//A.4.1.3
#define episode_set_bit		__set_bit_le

#define episode_set_bit(nr, addr)		\
	__set_bit((nr), (unsigned long *)(addr))

//A5 splice_branch 解决alloc_branch的遗留问题:(1)前面alloc_brach得到的链表可能是断裂的，链表头部没有插入到对应的位置，或者没有分配间接块，而是直接分配了最末一级的数据块，这些数据块并没有连到前面的多级索引上
//https://blog.csdn.net/kai_ding/article/details/9737605
static inline int splice_branch(struct inode *inode,
				     Indirect chain[DEPTH],
				     Indirect *where,
				     int num)
{
	int i;

	write_lock(&pointers_lock);

	/* Verify that place we are splicing to is still there and vacant */
	if (!verify_chain(chain, where-1) || *where->p)
		goto changed;

	*where->p = where->key;//修复映射断裂，在索引块的特定偏移位置（where->p指向）记录被索引块的块号（无论被索引块是间接索引块还是直接数据块），这样，映射断裂处的断裂就不复存在了；

	write_unlock(&pointers_lock);

	/* We are done with atomic stuff, now do the rest of housekeeping */

	inode->i_ctime = current_time(inode);

	/* had we spliced it onto indirect block? */
	if (where->bh)
		mark_buffer_dirty_inode(where->bh, inode);

	mark_inode_dirty(inode);
	return 0;

changed:
	write_unlock(&pointers_lock);
	for (i = 1; i < num; i++)
		bforget(where[i].bh);
	for (i = 0; i < num; i++)
		episode_free_block(inode, block_to_cpu(where[i].key));//释放掉申请到的那num个block，主要是对inode进行处理。//A5.1
	return -EAGAIN;
}

//A5.1
void episode_free_block(struct inode *inode, unsigned long block)
{
	struct super_block *sb = inode->i_sb;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct buffer_head *bh;
	int k = sb->s_blocksize_bits + 3;//相当于×8，
	unsigned long bit, zone;

	if (block < sbi->s_firstdatazone || block >= sbi->s_nzones) {
		printk("Trying to free block not in datazone\n");
		return;
	}
	zone = block - sbi->s_firstdatazone + 1;//相对块号，
	bit = zone & ((1<<k) - 1);//找到要释放的block在zmap中的bit位（块内偏移量）
	zone >>= k;//找到要释放的block所在的zmap的块号
	if (zone >= sbi->s_zmap_blocks) {
		printk("episode_free_block: nonexistent bitmap buffer\n");
		return;
	}
	bh = sbi->s_zmap[zone];//找到要释放的block对应的block在缓冲的缓冲头bh
	spin_lock(&bitmap_lock);
	if (!episode_test_and_clear_bit(bit, bh->b_data))//对bh对应的缓冲块中的对应位置清0，表示这个block没有被用
		printk("episode_free_block (%s:%lu): bit already cleared\n",
		       sb->s_id, block);
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);//标记被释放的block对应的缓冲头已经发生更改。
	return;
}
