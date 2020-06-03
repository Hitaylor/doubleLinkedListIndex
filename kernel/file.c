#include "episode.h"
#include <uapi/linux/uio.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>
#include <linux/hash.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/security.h>
#include <linux/cpuset.h>
#include <linux/hardirq.h> /* for BUG_ON(!in_atomic()) only */
#include <linux/hugetlb.h>
#include <linux/memcontrol.h>
#include <linux/cleancache.h>
#include <linux/shmem_fs.h>
#include <linux/rmap.h>
//#include <sys/user.h>
//#include "itree.h"
static ssize_t episode_file_buffered_read(struct kiocb *iocb, struct iov_iter *iter, ssize_t written);
static void shrink_readahead_size_eio(struct file *filp, struct file_ra_state *ra);
                
static int episode_setattr(struct dentry *dentry, struct iattr *attr)
{
    struct inode *inode = d_inode(dentry);
    int error;

    error = setattr_prepare(dentry, attr);
    if (error)
      return error;

    if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size != i_size_read(inode)) {
      error = inode_newsize_ok(inode, attr->ia_size);
      if (error)
        return error;

      truncate_setsize(inode, attr->ia_size);
      episode_truncate(inode);
    }

    setattr_copy(inode, attr);
    mark_inode_dirty(inode);
    return 0;
}

static inline int set_iocb_flags(struct file *file)
{
        int res = 0;
	res |= IOCB_APPEND;
	res |= IOCB_DIRECT;
	res |= IOCB_SYNC;
        return res;
}

static inline enum rw_hint write_hint(struct file *file)
{
        if (file->f_write_hint != WRITE_LIFE_NOT_SET)
                return file->f_write_hint;

        return file_inode(file)->i_write_hint;
}

static inline void init_kiocb(struct kiocb *kiocb, struct file *filp)
{
        *kiocb = (struct kiocb) {
                .ki_filp = filp,
                .ki_flags = set_iocb_flags(filp),
                .ki_hint = write_hint(filp),
        };
}


/**
 * 当前基于buf中数据组织格式为：dataLen data dataLen data ...这种形式
 * 经过本函数内的封装之后，数据格式为： 
 * Prev next   timestamp        offset  lenth   data
 * 8    8       4               8       4       k    
 * offset目前指向prev的开头位置
 * 这里需要注意的是：buf的大小应该是512的2^n的倍数。也就是说，buf中前面的数据是没有填充的，之后最后一条数据的后面的填充的，也就是说buf中除了最后一条数据之后可能存在用0填充之外，其余地方不能存在洞。所以，我们在构造buff的时候
 * 也要注意处理这方面的内容。也就是buf中有一部分内容是空的。如果我们构造的buff不是512的2^n倍数，则可能写不进去。
 * 目前的版本，要求buf要有足够的余量来容纳扩展字段。
 */
static ssize_t episode_direct_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
        //构造新的buff，然后遍历buf，创建新的记录，填充到buff中。jsc 0510
        /*******************************************************************
         * Prev next   timestamp        offset  len   data
         * 8    8       4               8       4       k       
         * ****************************************************************/
        char * buff = NULL, *buff2=NULL;
       
        __u32 pos = 0;//buf内的偏移量
       // __u32  dataSizeinRec = 0;//buf内当前数据记录的数据段长度
        __u32 tmp= 0;
        __u32 bufLen = len,writeLen=0;//buf的长度
        __u32 timestamp = 0;//时间戳
        __u64 position=0;//与prev、next类型一致,nextPosition用于buff2中记录next字段的位置
        __u64 prev = 0,next = 0,offset = 0;//在文件中，当前记录的前一个记录的起始位置、next字段起始位置，以及当前记录的数据段位置
        char  lenSeg[4]={0},time[sizeof(timestamp)]={0};//用于记录长度和时间的临时变量
        __u32  recLen=0,lastRecLen=0,recNum=0,additionalLen=0,totalNeedforBuff2=0;//lastRecLen记录上一条数据的长度，用途是用于确定最后一条数据的next字段的起始位置
        char * ptr8;
        int i,testcount=0;
        __u64 tmpLen;
        __u64 curPos,basePos;
        __u64 lastRecPos = 0;
        int retnum = 0;
	//读取inode中的上一条记录的位置，jsc 0510
        
        struct episode_inode * raw_inode;
        struct inode * inode = file_inode(filp);
	struct episode_inode_info *episode_inode = episode_i(inode);
        
        struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;
struct iovec iov;

        if(!inode){
               printk("The inode got from the filp %s is null!\n",filp->f_path.dentry->d_iname); 
        }
        if(!episode_inode){
                printk("The episode_inode got from the inode is null!\n"); 
        }
        additionalLen = sizeof(prev)+sizeof(next)+sizeof(offset)+sizeof(time);
        printk("I am here! and the user buf size is %d\n" ,len);
        lastRecPos = episode_inode->i_lastrecordpos;
        tmpLen = i_lastrecordpos(inode);
        curPos = inode->i_size;//文件游标位置,也是这次写操作的base
        basePos = curPos;
        printk("The last data record position: %ld\t tmpLen=%lld\n current postion:%ld\n",lastRecPos,tmpLen,curPos);
        if(len%512 != 0)
        {
           printk("user buf len %lu mod 512!=0, now return -1! \n",len);
           return -1;
        }
        printk("len=%d\n",len);
        //这里，后续可以先读取一遍buf，得到具体的record数量n，因为对于每条record，扩展需要添加的字节数是固定的，8+8+8+4=28字节，则比原来需要增加28n字节，然后将len+28n向上取512的整数倍，即为buff的长度
       
        //buff =(char *) kmalloc((1+len/512)*512, GFP_KERNEL);//buff存储的内容和buf一样，只不过是内核态的空间
        buff = (char*)kmalloc(len,GFP_KERNEL);
        //printk("ater kmalloc, buff:");
       // for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
        memset(buff,0,len);
        // printk("ater memset, buff:");
        //for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
        if(!buff)
        {
           printk("kmalloc failed for the buff!\n");
        }
        printk("buff size %d\n",sizeof(buff));
        //给buff赋值
        retnum = copy_from_user(buff,buf,1024);
        //printk("ater copy from user, buff:");
        //for(i=0; i<len; i++) printk("buff[%d]=%d, ",i,buff[i]);
        printk(" I am here 3! retnum=%d for the function copy_from_user(). pos=%d, bufLen=%d\n",retnum,pos,bufLen);
        //遍历buff，获取记录数，并确定要扩展的长度，最终确定buff2有多长
        pos=0;
        while(pos<bufLen-1){
           memcpy(&recLen,&buff[pos],sizeof(recLen));
           if(recLen == 0){
                break;
           }
           pos = pos+recLen+sizeof(recLen);
           recNum++;
        }
        totalNeedforBuff2 = pos+recNum*additionalLen;
        if(totalNeedforBuff2%512!=0){
           totalNeedforBuff2 = (1+(int)totalNeedforBuff2/512)*512;
        }
        //因为当前版本中，对于buff2长度超过buf的时候，没法处理（因为要把buff2的内容通过copy to user赋值给buf，就会出现溢出），所以，这里加判断，是否会溢出，溢出则返回。
        if(totalNeedforBuff2 > len) {
           printk("Not enough space for index extention in the buf!!!");
           return -1;
        }
         printk("total need for buff2 is %ld, and there are %d records in buf, real data in buf is %d!\n",totalNeedforBuff2,recNum,pos);
        buff2 = (char *)kmalloc(len, GFP_KERNEL);
        memset(buff2,0,len);
        
       // printk("size of buff2:%d\n",sizeof(buff2));
        
        pos = 0;
        //这里bufLen=0,是有问题的
        while(pos < bufLen-1){ //遍历buf中的每一条记录，进行扩充，形成新的结构，然后放到buff中。
          // mid_char(&lenSeg[0], buf, 4, pos);//获取buf中一条记录的长度字段
          // printk("Address of reLen : %x, buff:%x\n",&recLen,buff);
           memcpy(&recLen,&buff[pos],sizeof(recLen));
           printk(" I am here 4! and recLen=%d\n",recLen);
           if(recLen == 0) {
                printk("Reach the end of the records in buff!");
                //Todo 设定本buff2中最后一条数据的next指向为下一个buff2的第8字节
                next = basePos+len+sizeof(next);
                printk("next=%ul\n",next);
               // memcpy(&buff2[curPos-lastRecLen - sizeof(recLen)-sizeof(timestamp)-sizeof(offset)-sizeof(next)],&next,sizeof(next));
                memcpy(&buff2[lastRecPos-basePos+sizeof(prev)],&next,sizeof(next));
                break;//跳出while，也就是buf中已经没有新记录了。
           }
          
           //构造索引结构和索引信息
           //prev,next,timestamp,offset,len,data
           prev = lastRecPos;
           next = curPos+sizeof(prev)+sizeof(next)+sizeof(timestamp)+sizeof(offset)+sizeof(recLen)+recLen+sizeof(prev);
          // nextPosition =
           timestamp = getCurrentTime();
           offset = curPos;
           //printk("Current time: %d, prev=%ld, next=%ld,recLen=%d,offset=%ld, and prev position=%ld\n",timestamp,prev,next,recLen,offset,position);
          
       
           memcpy(&buff2[position],&prev,sizeof(prev));
           printk("prev segment:");
           for(i=0;i<sizeof(prev);i++) printk("buff2[%d]=%ud",position+i,buff2[position+i]<0 ? (255+buff2[position+i]):buff2[position+i]);
           position = position +sizeof(prev);
                   
           memcpy(&buff2[position],&next,sizeof(next));
           printk("next segment:");
           for(i=0;i<sizeof(next);i++) printk("buff2[%d]=%ud",position+i,buff2[position+i]<0 ? (255+buff2[position+i]):buff2[position+i]);
           position = position +sizeof(next);
         
           memcpy(&buff2[position],&timestamp,sizeof(timestamp));
           printk("timestamp segment:");
           for(i=0;i<sizeof(timestamp);i++) printk("buff2[%d]=%ud",position+i,buff2[position+i]<0 ? (255+buff2[position+i]):buff2[position+i]);
           position = position +sizeof(timestamp);
          
           memcpy(&buff2[position],&curPos,sizeof(curPos));
           printk("offset segment:");
           for(i=0;i<sizeof(offset);i++) printk("buff2[%d]=%ud",position+i,buff2[position+i]<0 ? (255+buff2[position+i]):buff2[position+i]);
           position = position +sizeof(curPos);
           
           
           //ptr8 = NULL;
           /*
           memcpy(&buff[position],lenSeg,sizeof(dataSizeinRec));
           position = position +4;
           memcpy(&buff[position],&buf[4],len-4);
           position = position +len-4;
           */
          //copy buf中该数据记录到buff中,替换上面的两次memcpy()调用
          memcpy(&buff2[position],&buff[pos],sizeof(recLen)+recLen);
          printk("recLen segment:");
           for(i=0;i<sizeof(recLen);i++) printk("buff2[%d]=%ud",position+i,buff2[position+i]<0 ? (255+buff2[position+i]):buff2[position+i]);
          printk("data segment:");
           for(i=0;i<recLen; i++) printk("buff2[%d]=%c",position+i+sizeof(recLen),buff2[position+i+sizeof(recLen)]);
          position = position+recLen+sizeof(recLen);
          
          lastRecPos = curPos;
          curPos =curPos+ sizeof(prev)+sizeof(next)+sizeof(timestamp)+sizeof(offset)+sizeof(recLen)+recLen;
          
           printk("The start position of the next extended record is %ld\n",curPos);
           pos = pos+sizeof(recLen)+recLen;
           lastRecLen = recLen;
           recLen = 0;
           printk("current pos:%d\n",pos);
        }
        //Todo 增加最后一条数据的处理机制，指向本buf结束后的下一个位置。
        

        printk("After extending,data size in buff2 is %d, data in buff2 are:\n",position);
        for(i=0;i<position;i++){
                
                if(buff2[i]<123 && buff2[i]>96) printk("buff2[%d]=%u, =%c",i,buff2[i]<0? (255+buff2[i]):buff2[i],buff2[i]);
                else if(buff2[i]<91 && buff2[i]>64) printk("buff2[%d]=%u, =%c",i,buff2[i]<0? (255+buff2[i]):buff2[i],buff2[i]);
                else if(buff2[i]<58 && buff2[i]>47) printk("buff2[%d]=%u, =%c",i,buff2[i]<0? (255+buff2[i]):buff2[i],buff2[i]);
                else printk("buff2[%d]=%u,",i,buff2[i]<0? (255+buff2[i]):buff2[i]);
        }
        printk("\n");
        retnum = clear_user(buf,len);//这里不敢把len改成其他长度，所以这里是有坑的！！！因为buf是用户态的，我们最多清理len长度。如果清理过长，会不会出问题？
        printk("bytes can not be cleared in the user buf is %d/%d\n",retnum,len);
        //retnum = copy_to_user(buf,buff2,totalNeedforBuff2);
        retnum = copy_to_user(buf,buff2,len);//这里按照buff2的实际长度给用户态buf赋值。totalNeedforBuff2如果和len不相等，不知道会不会有问题。最好是len长一些。
        printk("bytes cannot be copied to user space is retnum=%d for the function copty_to_user()\n",retnum);
        //再次将buf中的内容copy到buff中，测试buf中是否有内容
       
       /*  printk("pos = %d\n",pos);
          memset(buff,0,(1+len/512)*512);//和前面一致，要进行清零
      
        for(i=0;i<(1+len/512)*512;i++) {
                if(buff[i]==0) testcount++;
        }
        printk("after the memset, there are %d zeroes!\n",testcount);
        retnum = copy_from_user(buff,buf,len);
        printk("bytes cannot be copied to kernel space is %d/%d for the function copty_from_user()\n",retnum,len);
        testcount = 0;
        for(i=0;i<len;i++) {
                if(buff[i]==0) testcount++;
                printk("buff[%d]=%u",i,buff[i]<0? (255+buff[i]):buff[i]);
        }
        printk("after the copy_from_user, there are %d zeroes!\n",testcount);
        */
        //修改buf，jsc
       /* if(len<totalNeedforBuff2) writeLen = totalNeedforBuff2;
        else writeLen = len;*/
        writeLen = len;

       // struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
        iov.iov_base = (void __user *)buf;
        //iov.iov_len = position+1;//此时buff的长度应为512整数倍。但这里的position+1却不一定，所以这里填写什么需要确认一下
	//iov.iov_len = len;
       iov.iov_len = writeLen; //这里也要进行修改

	init_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	//iov_iter_init(&iter, WRITE, &iov, 1, len);//
iov_iter_init(&iter, WRITE, &iov, 1, writeLen);//
	ret = generic_file_write_iter(&kiocb, &iter);

        BUG_ON(ret == -EIOCBQUEUED);
        if (ret > 0){
                *ppos = kiocb.ki_pos;
                //最后一条的写入位置，即offset需要保留到inode中。
                episode_inode->i_lastrecordpos = offset;//这个不知道能不能写回？
        }
        
        kfree(buff);
        kfree(buff2);
        return ret;
}


static ssize_t episode_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
  size_t count = iov_iter_count(iter);//获取要读到的遍历器指向的用户空间缓冲区的长度，即iov_inter中的count字段,//linux/uio.h中
	ssize_t retval = 0;

	if (!count)
		goto out; /* skip atime */
  //init_sync_kiocb中对ki_flags进行了赋值。iocb.ki_flags = iocb_flags(filp)
  //暂不支持DIRECT，只使用缓存
	if (iocb->ki_flags & IOCB_DIRECT) {//如果文件打开方式中用了DIRECT标记
	       retval = -1;
                goto out;
	}

	retval = episode_file_buffered_read(iocb, iter, retval);//7.我们一般采用这种方式读取数据到用户态的缓冲区
out:
	return retval;      
}


/**
 * episode_file_buffered_read - episode file read routine
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
static ssize_t episode_file_buffered_read(struct kiocb *iocb,
		struct iov_iter *iter, ssize_t written)
{
	struct file *filp = iocb->ki_filp;//从iocb中拿到文件指针，
	struct address_space *mapping = filp->f_mapping;//获得文件对应的address_space对象，其实就是inode对应的address_space对象。
	struct inode *inode = mapping->host;//该address_space 对象mapping对应的inode
	struct file_ra_state *ra = &filp->f_ra;//文件预读相关的
	loff_t *ppos = &iocb->ki_pos; //long long 类型
	pgoff_t index; //unsigned long类型，该页描述结构在地址空间radix树page_tree中的对象索引号即页号, 表示该页在vm_file中的偏移页数
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
		error = mapping->a_ops->readpage(filp, page); //8. 调用episode的readpage（）函数

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


/*
 * CD/DVDs are error prone. When a medium error occurs, the driver may fail
 * a _large_ part of the i/o request. Imagine the worst scenario:
 *
 *      ---R__________________________________________B__________
 *         ^ reading here                             ^ bad block(assume 4k)
 *
 * read(R) => miss => readahead(R...B) => media error => frustrating retries
 * => failing the whole request => read(R) => read(R+1) =>
 * readahead(R+1...B+1) => bang => read(R+2) => read(R+3) =>
 * readahead(R+3...B+2) => bang => read(R+3) => read(R+4) =>
 * readahead(R+4...B+3) => bang => read(R+4) => read(R+5) => ......
 *
 * It is going insane. Fix it by quickly scaling down the readahead size.
 */
static void shrink_readahead_size_eio(struct file *filp,
					struct file_ra_state *ra)
{
	ra->ra_pages /= 4;
}



/*
 * We have mostly NULLs here: the current defaults are OK for
 * the episode filesystem.
 */
const struct file_operations episode_file_operations = {
    .llseek	= generic_file_llseek,
   // .read_iter	= generic_file_read_iter,
    .read_iter	= episode_file_read_iter,
    .write	= episode_direct_write,
    .mmap	= generic_file_mmap,
    .fsync		= generic_file_fsync,
    .splice_read	= generic_file_splice_read,
};

const struct inode_operations episode_file_inode_operations = {
	.setattr	= episode_setattr,
	.getattr	= episode_getattr,
};
