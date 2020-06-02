读数据时，进行read函数调用，进入陷阱门调用到函数：

1.SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count) // (/fs/read_write.c) 
2.--> ret = vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos);
3. -->__vfs_read()
    -->if (file->f_op->read)
        return file->f_op->read(file, buf, count, pos);
      else if (file->f_op->read_iter)
4.      return new_sync_read(file, buf, count, pos);
5.     -->call_read_iter(filp, &kiocb, &iter);
6.      -->return file->f_op->read_iter(kio, iter);//6.
6.         等同于generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
              if (iocb->ki_flags & IOCB_DIRECT) {//一般不会用到direct读
                retval = mapping->a_ops->direct_IO(iocb, iter);//6.1我们没有设定此函数
              }
              retval = generic_file_buffered_read(iocb, iter, retval);//7.一般采用这种缓存读方式
7.

在linux3.16后文件系统的file_operations操作一般已经不使用操作read函数操作了，可以进入到源码/fs下面的文件系统的file.c里面看下。

1. SYSCALL_DEFINE3
SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos = file_pos_read(f.file);
		ret = vfs_read(f.file, buf, count, &pos);//2.vfs_read()
		if (ret >= 0)
			file_pos_write(f.file, pos);//更改file中的读写位置字段
		fdput_pos(f);//更新f
	}
	return ret;
}
2. vfs_read
ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(VERIFY_WRITE, buf, count)))
		return -EFAULT;
//以上为权限检查
	ret = rw_verify_area(READ, file, pos, count);//参数检查,如读写位置是否为负,或者读的数量超过文件自身字节数上限,如果该函数执行出错就直接就退出读操作了
	if (!ret) {
		if (count > MAX_RW_COUNT)
			count =  MAX_RW_COUNT;
		ret = __vfs_read(file, buf, count, pos);//3.
		if (ret > 0) {
			fsnotify_access(file);
			add_rchar(current, ret);
		}
		inc_syscr(current);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(vfs_read);

3. __vfs_read
ssize_t __vfs_read(struct file *file, char __user *buf, size_t count,
		   loff_t *pos)
{
	if (file->f_op->read)
		return file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)//episode配置的是这个
		return new_sync_read(file, buf, count, pos); //4.
	else
		return -EINVAL;
}

4. new_sync_read
static ssize_t new_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };//存放用户空间传入的数据和长度,也就是读len字节到buf中
	struct kiocb kiocb;//kernel i/o control block
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);//4.1 绑定filp和kiocb;初始化描述符 kiocb，并设置 ki_key 字段为 KIOCB_SYNC_KEY、ki_flip 字段为 filp、ki_obj 字段为 current。
	kiocb.ki_pos = *ppos;
	iov_iter_init(&iter, READ, &iov, 1, len);//初始化遍历器,参考https://blog.csdn.net/jinking01/article/details/106474891

	ret = call_read_iter(filp, &kiocb, &iter);//5.调用真正的读函数
	BUG_ON(ret == -EIOCBQUEUED);
	*ppos = kiocb.ki_pos;
	return ret;
}

4.1 init_sync_kiocb
static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb) {
		.ki_filp = filp,
		.ki_flags = iocb_flags(filp),//4.1.1从filp获取用户指定的打开标记
		.ki_hint = file_write_hint(filp),
	};
}

4.1.1 iocb_flags
static inline int iocb_flags(struct file *file)
{
	int res = 0;
	if (file->f_flags & O_APPEND)
		res |= IOCB_APPEND;
	if (io_is_direct(file))
		res |= IOCB_DIRECT;
	if ((file->f_flags & O_DSYNC) || IS_SYNC(file->f_mapping->host))
		res |= IOCB_DSYNC;
	if (file->f_flags & __O_SYNC)
		res |= IOCB_SYNC;
	return res;
}
以下这几个变量都不存在重合，所以可以进行或操作
#define IOCB_EVENTFD		(1 << 0)
#define IOCB_APPEND		(1 << 1)
#define IOCB_DIRECT		(1 << 2)
#define IOCB_HIPRI		(1 << 3)
#define IOCB_DSYNC		(1 << 4)
#define IOCB_SYNC		(1 << 5)
#define IOCB_WRITE		(1 << 6)
#define IOCB_NOWAIT		(1 << 7)


5. call_read_iter
static inline ssize_t call_read_iter(struct file *file, struct kiocb *kio,
				     struct iov_iter *iter)
{
	return file->f_op->read_iter(kio, iter);//6.调用episode指定的read_iter函数
}


6. generic_file_read_iter，这个可能是我们要重写的，连同其下面调用的函数都要改写。

/**
 * generic_file_read_iter - generic filesystem read routine
 * @iocb:	kernel I/O control block
 * @iter:	destination for the data read
 *
 * This is the "read_iter()" routine for all filesystems
 * that can use the page cache directly.
 */
ssize_t
generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);//获取要读到的遍历器指向的用户空间缓冲区的长度，即iov_inter中的count字段
	ssize_t retval = 0;

	if (!count)
		goto out; /* skip atime */
  //init_sync_kiocb中对ki_flags进行了赋值。iocb.ki_flags = iocb_flags(filp)
  分为DIRECT 和 使用缓存
下面分成2类： DIRECT（if判定的）和使用缓存的方式。DIRECT方式指open时使用O_DIRECT, 它与使用缓存的区别就是不使用内核提供的缓存模式，绕过缓存模式，直接进行磁盘的读写操作。有特殊需要的可以使用这种模式进行操作。
	if (iocb->ki_flags & IOCB_DIRECT) {//如果文件打开方式中用了DIRECT标记
		struct file *file = iocb->ki_filp;
		struct address_space *mapping = file->f_mapping;
		struct inode *inode = mapping->host;
		loff_t size;

		size = i_size_read(inode);
		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_has_page(mapping, iocb->ki_pos,
						   iocb->ki_pos + count - 1))
				return -EAGAIN;
		} else {
			retval = filemap_write_and_wait_range(mapping,
						iocb->ki_pos,
					        iocb->ki_pos + count - 1);
			if (retval < 0)
				goto out;
		}

		file_accessed(file);

		retval = mapping->a_ops->direct_IO(iocb, iter);//6.1
		if (retval >= 0) {
			iocb->ki_pos += retval;
			count -= retval;
		}
		iov_iter_revert(iter, count - iov_iter_count(iter));

		/*
		 * Btrfs can have a short DIO read if we encounter
		 * compressed extents, so if there was an error, or if
		 * we've already read everything we wanted to, or if
		 * there was a short read because we hit EOF, go ahead
		 * and return.  Otherwise fallthrough to buffered io for
		 * the rest of the read.  Buffered reads will not work for
		 * DAX files, so don't bother trying.
		 */
		if (retval < 0 || !count || iocb->ki_pos >= size ||
		    IS_DAX(inode))
			goto out;
	}

	retval = generic_file_buffered_read(iocb, iter, retval);//7.我们一般采用这种方式读取数据到用户态的缓冲区
out:
	return retval;
}
EXPORT_SYMBOL(generic_file_read_iter);

7. generic_file_buffered_read 这个不是导出函数，我们很可能要对它进行改名改写。在里面进行数据解封装。

/**
 * generic_file_buffered_read - generic file read routine
 * @iocb:	the iocb to read，iocb是和filp绑定的，
 * @iter:	data destination，与用户态的一块buf绑定
 * @written:	already copied
 *
 * This is a generic file read routine, and uses the
 * mapping->a_ops->readpage() function for the actual low-level stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 */
static ssize_t generic_file_buffered_read(struct kiocb *iocb,
		struct iov_iter *iter, ssize_t written)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct file_ra_state *ra = &filp->f_ra;
	loff_t *ppos = &iocb->ki_pos;
	pgoff_t index;
	pgoff_t last_index;
	pgoff_t prev_index;
	unsigned long offset;      /* offset into pagecache page */
	unsigned int prev_offset;
	int error = 0;

	if (unlikely(*ppos >= inode->i_sb->s_maxbytes))
		return 0;
	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);

	index = *ppos >> PAGE_SHIFT;
	prev_index = ra->prev_pos >> PAGE_SHIFT;
	prev_offset = ra->prev_pos & (PAGE_SIZE-1);
	last_index = (*ppos + iter->count + PAGE_SIZE-1) >> PAGE_SHIFT;
	offset = *ppos & ~PAGE_MASK;

	for (;;) {
		struct page *page;
		pgoff_t end_index;
		loff_t isize;
		unsigned long nr, ret;

		cond_resched();
find_page:
		if (fatal_signal_pending(current)) {
			error = -EINTR;
			goto out;
		}

		page = find_get_page(mapping, index);
		if (!page) {
			if (iocb->ki_flags & IOCB_NOWAIT)
				goto would_block;
			page_cache_sync_readahead(mapping,
					ra, filp,
					index, last_index - index);
			page = find_get_page(mapping, index);
			if (unlikely(page == NULL))
				goto no_cached_page;
		}
		if (PageReadahead(page)) {
			page_cache_async_readahead(mapping,
					ra, filp, page,
					index, last_index - index);
		}
		if (!PageUptodate(page)) {
			if (iocb->ki_flags & IOCB_NOWAIT) {
				put_page(page);
				goto would_block;
			}

			/*
			 * See comment in do_read_cache_page on why
			 * wait_on_page_locked is used to avoid unnecessarily
			 * serialisations and why it's safe.
			 */
			error = wait_on_page_locked_killable(page);
			if (unlikely(error))
				goto readpage_error;
			if (PageUptodate(page))
				goto page_ok;

			if (inode->i_blkbits == PAGE_SHIFT ||
					!mapping->a_ops->is_partially_uptodate)
				goto page_not_up_to_date;
			/* pipes can't handle partially uptodate pages */
			if (unlikely(iter->type & ITER_PIPE))
				goto page_not_up_to_date;
			if (!trylock_page(page))
				goto page_not_up_to_date;
			/* Did it get truncated before we got the lock? */
			if (!page->mapping)
				goto page_not_up_to_date_locked;
			if (!mapping->a_ops->is_partially_uptodate(page,
							offset, iter->count))
				goto page_not_up_to_date_locked;
			unlock_page(page);
		}
page_ok:
		/*
		 * i_size must be checked after we know the page is Uptodate.
		 *
		 * Checking i_size after the check allows us to calculate
		 * the correct value for "nr", which means the zero-filled
		 * part of the page is not copied back to userspace (unless
		 * another truncate extends the file - this is desired though).
		 */

		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_SHIFT;
		if (unlikely(!isize || index > end_index)) {
			put_page(page);
			goto out;
		}

		/* nr is the maximum number of bytes to copy from this page */
		nr = PAGE_SIZE;
		if (index == end_index) {
			nr = ((isize - 1) & ~PAGE_MASK) + 1;
			if (nr <= offset) {
				put_page(page);
				goto out;
			}
		}
		nr = nr - offset;

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		/*
		 * When a sequential read accesses a page several times,
		 * only mark it as accessed the first time.
		 */
		if (prev_index != index || offset != prev_offset)
			mark_page_accessed(page);
		prev_index = index;

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 */

		ret = copy_page_to_iter(page, offset, nr, iter);
		offset += ret;
		index += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;
		prev_offset = offset;

		put_page(page);
		written += ret;
		if (!iov_iter_count(iter))
			goto out;
		if (ret < nr) {
			error = -EFAULT;
			goto out;
		}
		continue;

page_not_up_to_date:
		/* Get exclusive access to the page ... */
		error = lock_page_killable(page);
		if (unlikely(error))
			goto readpage_error;

page_not_up_to_date_locked:
		/* Did it get truncated before we got the lock? */
		if (!page->mapping) {
			unlock_page(page);
			put_page(page);
			continue;
		}

		/* Did somebody else fill it already? */
		if (PageUptodate(page)) {
			unlock_page(page);
			goto page_ok;
		}

readpage:
		/*
		 * A previous I/O error may have been due to temporary
		 * failures, eg. multipath errors.
		 * PG_error will be set again if readpage fails.
		 */
		ClearPageError(page);
		/* Start the actual read. The read will unlock the page. */
		error = mapping->a_ops->readpage(filp, page);

		if (unlikely(error)) {
			if (error == AOP_TRUNCATED_PAGE) {
				put_page(page);
				error = 0;
				goto find_page;
			}
			goto readpage_error;
		}

		if (!PageUptodate(page)) {
			error = lock_page_killable(page);
			if (unlikely(error))
				goto readpage_error;
			if (!PageUptodate(page)) {
				if (page->mapping == NULL) {
					/*
					 * invalidate_mapping_pages got it
					 */
					unlock_page(page);
					put_page(page);
					goto find_page;
				}
				unlock_page(page);
				shrink_readahead_size_eio(filp, ra);
				error = -EIO;
				goto readpage_error;
			}
			unlock_page(page);
		}

		goto page_ok;

readpage_error:
		/* UHHUH! A synchronous read error occurred. Report it */
		put_page(page);
		goto out;

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 */
		page = page_cache_alloc(mapping);
		if (!page) {
			error = -ENOMEM;
			goto out;
		}
		error = add_to_page_cache_lru(page, mapping, index,
				mapping_gfp_constraint(mapping, GFP_KERNEL));
		if (error) {
			put_page(page);
			if (error == -EEXIST) {
				error = 0;
				goto find_page;
			}
			goto out;
		}
		goto readpage;
	}

would_block:
	error = -EAGAIN;
out:
	ra->prev_pos = prev_index;
	ra->prev_pos <<= PAGE_SHIFT;
	ra->prev_pos |= prev_offset;

	*ppos = ((loff_t)index << PAGE_SHIFT) + offset;
	file_accessed(filp);
	return written ? written : error;
}
