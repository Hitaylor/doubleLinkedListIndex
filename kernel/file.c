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
static size_t copy_from_iter_iovec1(void *to, size_t bytes, struct iov_iter *i);
static size_t copy_from_iter_iovec_to_kernel_buf(void *to, size_t bytes, struct iov_iter *i);
static size_t memcpy_from_iovec(char *kdata, struct iovec *iov, size_t len);
static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);
static void memcpy_from_page(char *to, struct page *page, size_t offset, size_t len)
{
	char *from = kmap_atomic(page);
	memcpy(to, from + offset, len);
	kunmap_atomic(from);
}
static void memcpy_to_page(struct page * page, size_t offset, const char * from, size_t len)
{
	char * to = kmap_atomic(page);
	memcpy(to+offset, from, len );
	kunmap_atomic(to);
}

static int copyin(void *to, const void __user *from, size_t n)
{
	if (access_ok(VERIFY_READ, from, n)) {
		kasan_check_write(to, n);
		n = raw_copy_from_user(to, from, n);
	}
	return n;
}

static int episode_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size != i_size_read(inode))
	{
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
	*kiocb = (struct kiocb){
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
	char *buff = NULL, *buff2 = NULL;

	__u32 pos = 0; //buf内的偏移量
								 // __u32  dataSizeinRec = 0;//buf内当前数据记录的数据段长度
	__u32 tmp = 0;
	__u32 bufLen = len, writeLen = 0;																												//buf的长度
	__u32 timestamp = 0;																																		//时间戳
	__u64 position = 0;																																			//与prev、next类型一致,nextPosition用于buff2中记录next字段的位置
	__u64 prev = 0, next = 0, offset = 0;																										//在文件中，当前记录的前一个记录的起始位置、next字段起始位置，以及当前记录的数据段位置
	char lenSeg[4] = {0}, time[sizeof(timestamp)] = {0};																		//用于记录长度和时间的临时变量
	__u32 recLen = 0, lastRecLen = 0, recNum = 0, additionalLen = 0, totalNeedforBuff2 = 0; //lastRecLen记录上一条数据的长度，用途是用于确定最后一条数据的next字段的起始位置
	char *ptr8;
	int i, testcount = 0;
	__u64 tmpLen;
	__u64 curPos, basePos;
	__u64 lastRecPos = 0;
	int retnum = 0;
	//读取inode中的上一条记录的位置，jsc 0510

	struct episode_inode *raw_inode;
	struct inode *inode = file_inode(filp);
	struct episode_inode_info *episode_inode = episode_i(inode);

	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;
	struct iovec iov;

	if (!inode)
	{
		printk("The inode got from the filp %s is null!\n", filp->f_path.dentry->d_iname);
	}
	if (!episode_inode)
	{
		printk("The episode_inode got from the inode is null!\n");
	}
	additionalLen = sizeof(prev) + sizeof(next) + sizeof(offset) + sizeof(time);
	//printk("I am here! and the user buf size is %d\n", len);
	lastRecPos = episode_inode->i_lastrecordpos;
	tmpLen = i_lastrecordpos(inode);
	curPos = inode->i_size; //文件游标位置,也是这次写操作的base
	basePos = curPos;
	//printk("The last data record position: %ld\t tmpLen=%lld\n current postion:%ld\n", lastRecPos, tmpLen, curPos);
	if (len % 512 != 0)
	{
		printk("user buf len %lu mod 512!=0, now return -1! \n", len);
		return -1;
	}
	//printk("len=%d\n", len);
	//这里，后续可以先读取一遍buf，得到具体的record数量n，因为对于每条record，扩展需要添加的字节数是固定的，8+8+8+4=28字节，则比原来需要增加28n字节，然后将len+28n向上取512的整数倍，即为buff的长度

	//buff =(char *) kmalloc((1+len/512)*512, GFP_KERNEL);//buff存储的内容和buf一样，只不过是内核态的空间
	buff = (char *)kmalloc(len, GFP_KERNEL);
	//printk("ater kmalloc, buff:");
	// for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
	memset(buff, 0, len);
	// printk("ater memset, buff:");
	//for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
	if (!buff)
	{
		printk("kmalloc failed for the buff!\n");
	}
	//printk("buff size %d\n", sizeof(buff));
	//给buff赋值
	//retnum = copy_from_user(buff, buf, 1024);
	retnum = copy_from_user(buff, buf, len);
	//printk("ater copy from user, buff:");
	for(i=0; i<len; i++) printk("buff[%d]=%d, ",i,buff[i]);
	printk(" I am here 3! retnum=%d for the function copy_from_user(). pos=%d, bufLen=%d\n", retnum, pos, bufLen);
	//遍历buff，获取记录数，并确定要扩展的长度，最终确定buff2有多长
	pos = 0;
	while (pos < bufLen - 1)
	{
		memcpy(&recLen, &buff[pos], sizeof(recLen));
		if (recLen == 0)
		{
			break;
		}
		pos = pos + recLen + sizeof(recLen);
		recNum++;
	}
	totalNeedforBuff2 = pos + recNum * additionalLen;
	if (totalNeedforBuff2 % 512 != 0)
	{
		totalNeedforBuff2 = (1 + (int)totalNeedforBuff2 / 512) * 512;
	}
	//因为当前版本中，对于buff2长度超过buf的时候，没法处理（因为要把buff2的内容通过copy to user赋值给buf，就会出现溢出），所以，这里加判断，是否会溢出，溢出则返回。
	if (totalNeedforBuff2 > len)
	{
		printk("Not enough space for index extention in the buf!!!");
		return -1;
	}
	//printk("total need for buff2 is %ld, and there are %d records in buf, real data in buf is %d!\n", totalNeedforBuff2, recNum, pos);
	buff2 = (char *)kmalloc(len, GFP_KERNEL);
	memset(buff2, 0, len);

	// printk("size of buff2:%d\n",sizeof(buff2));

	pos = 0;//buff中的游标
	//这里bufLen=0,是有问题的
	while (pos < bufLen - 1)
	{ //遍历buf中的每一条记录，进行扩充，形成新的结构，然后放到buff中。
		// mid_char(&lenSeg[0], buf, 4, pos);//获取buf中一条记录的长度字段
		// printk("Address of reLen : %x, buff:%x\n",&recLen,buff);
		memcpy(&recLen, &buff[pos], sizeof(recLen));
		//printk(" I am here 4! and recLen=%d\n", recLen);
		if (recLen == 0)
		{
			//printk("Reach the end of the records in buff!");
			//Todo 设定本buff2中最后一条数据的next指向为下一个buff2的第8字节
			next = basePos + len + sizeof(next);
			//printk("next=%ul\n", next);
			// memcpy(&buff2[curPos-lastRecLen - sizeof(recLen)-sizeof(timestamp)-sizeof(offset)-sizeof(next)],&next,sizeof(next));
			memcpy(&buff2[lastRecPos - basePos + sizeof(prev)], &next, sizeof(next));
			break; //跳出while，也就是buf中已经没有新记录了。
		}

		//构造索引结构和索引信息
		//prev,next,timestamp,offset,len,data
		prev = lastRecPos;
		next = curPos + sizeof(prev) + sizeof(next) + sizeof(timestamp) + sizeof(offset) + sizeof(recLen) + recLen + sizeof(prev);
		// nextPosition =
		timestamp = getCurrentTime();
		offset = curPos;
		//printk("Current time: %d, prev=%ld, next=%ld,recLen=%d,offset=%ld, and prev position=%ld\n",timestamp,prev,next,recLen,offset,position);

		memcpy(&buff2[position], &prev, sizeof(prev));
		/*printk("prev segment:");
		for (i = 0; i < sizeof(prev); i++)
			printk("buff2[%d]=%ud", position + i, buff2[position + i] < 0 ? (255 + buff2[position + i]) : buff2[position + i]);
		*/
		position = position + sizeof(prev);

		memcpy(&buff2[position], &next, sizeof(next));
		/*printk("next segment:");
		for (i = 0; i < sizeof(next); i++)
			printk("buff2[%d]=%ud", position + i, buff2[position + i] < 0 ? (255 + buff2[position + i]) : buff2[position + i]);
		*/
		position = position + sizeof(next);

		memcpy(&buff2[position], &timestamp, sizeof(timestamp));
		/*printk("timestamp segment:");
		for (i = 0; i < sizeof(timestamp); i++)
			printk("buff2[%d]=%ud", position + i, buff2[position + i] < 0 ? (255 + buff2[position + i]) : buff2[position + i]);
		*/
		position = position + sizeof(timestamp);

		memcpy(&buff2[position], &curPos, sizeof(curPos));
		/*printk("offset segment:");
		for (i = 0; i < sizeof(offset); i++)
			printk("buff2[%d]=%ud", position + i, buff2[position + i] < 0 ? (255 + buff2[position + i]) : buff2[position + i]);
		*/
		position = position + sizeof(curPos);

		//ptr8 = NULL;
		/*
           memcpy(&buff[position],lenSeg,sizeof(dataSizeinRec));
           position = position +4;
           memcpy(&buff[position],&buf[4],len-4);
           position = position +len-4;
           */
		//copy buf中该数据记录到buff中,替换上面的两次memcpy()调用
		memcpy(&buff2[position], &buff[pos], sizeof(recLen) + recLen);
		/*printk("recLen segment:");
		for (i = 0; i < sizeof(recLen); i++)
			printk("buff2[%d]=%ud", position + i, buff2[position + i] < 0 ? (255 + buff2[position + i]) : buff2[position + i]);
		printk("data segment:");
		for (i = 0; i < recLen; i++)
			printk("buff2[%d]=%c", position + i + sizeof(recLen), buff2[position + i + sizeof(recLen)]);
		*/
		position = position + recLen + sizeof(recLen);

		lastRecPos = curPos;
		curPos = curPos + sizeof(prev) + sizeof(next) + sizeof(timestamp) + sizeof(offset) + sizeof(recLen) + recLen;

		//printk("The start position of the next extended record is %ld\n", curPos);
		pos = pos + sizeof(recLen) + recLen;
		lastRecLen = recLen;
		recLen = 0;
		//printk("current pos:%d\n", pos);
	}
	//Todo 增加最后一条数据的处理机制，指向本buf结束后的下一个位置。

	//printk("After extending,data size in buff2 is %d, data in buff2 are:\n", position);
	/*for (i = 0; i < position; i++)
	{

		if (buff2[i] < 123 && buff2[i] > 96)
			printk("buff2[%d]=%u, =%c", i, buff2[i] < 0 ? (255 + buff2[i]) : buff2[i], buff2[i]);
		else if (buff2[i] < 91 && buff2[i] > 64)
			printk("buff2[%d]=%u, =%c", i, buff2[i] < 0 ? (255 + buff2[i]) : buff2[i], buff2[i]);
		else if (buff2[i] < 58 && buff2[i] > 47)
			printk("buff2[%d]=%u, =%c", i, buff2[i] < 0 ? (255 + buff2[i]) : buff2[i], buff2[i]);
		else
			printk("buff2[%d]=%u,", i, buff2[i] < 0 ? (255 + buff2[i]) : buff2[i]);
	}
	printk("\n");*/
	retnum = clear_user(buf, len); //这里不敢把len改成其他长度，所以这里是有坑的！！！因为buf是用户态的，我们最多清理len长度。如果清理过长，会不会出问题？
	printk("bytes can not be cleared in the user buf is %d/%d\n", retnum, len);
	//retnum = copy_to_user(buf,buff2,totalNeedforBuff2);
	retnum = copy_to_user(buf, buff2, len); //这里按照buff2的实际长度给用户态buf赋值。totalNeedforBuff2如果和len不相等，不知道会不会有问题。最好是len长一些。
	printk("bytes cannot be copied to user space is retnum=%d for the function copty_to_user()\n", retnum);
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
	iov_iter_init(&iter, WRITE, &iov, 1, writeLen); //
	ret = generic_file_write_iter(&kiocb, &iter);

	BUG_ON(ret == -EIOCBQUEUED);
	if (ret > 0)
	{
		*ppos = kiocb.ki_pos;
		//最后一条的写入位置，即offset需要保留到inode中。
		episode_inode->i_lastrecordpos = offset; //这个不知道能不能写回？
	}

	kfree(buff);
	kfree(buff2);
	return ret;
}

static ssize_t episode_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter); //获取要读到的遍历器指向的用户空间缓冲区的长度，即iov_inter中的count字段,//linux/uio.h中

	ssize_t retval = 0;
	size_t retnum = 0, tmp = 0;
	char *buf;
	char __user * ubuf = iter->iov->iov_base;
	//size_t iovlen = it
	struct page **pages;
	struct page **pagevec;
	struct page * tmpage;
	char * buff;
	char * buff2;
		unsigned int curPos = 0,curPos2 = 0,i=0;//buff和buff2中的游标
	uint64_t prev,next,cur,base;
	uint32_t time,recLen,recNum = 0;

	size_t start  = 0;
struct file *filp = iocb->ki_filp;							 //从iocb中拿到文件指针，
	struct address_space *mapping = filp->f_mapping; 
struct iovec iov = iov_iter_iovec(iter);
printk("[episode_file_read_iter]  iter->iov->iov_base:%p,iter->iov->iov_len:%u",iter->iov->iov_base, iter->iov->iov_len);
printk("iov_base:%p, iov_len:%u",iov.iov_base, iov.iov_len);

	if (!count)
		goto out; /* skip atime */
							//init_sync_kiocb中对ki_flags进行了赋值。iocb.ki_flags = iocb_flags(filp)
							//暂不支持DIRECT，只使用缓存
	if (iocb->ki_flags & IOCB_DIRECT)
	{ //如果文件打开方式中用了DIRECT标记
		retval = -1;
		goto out;
	}
	printk("before read: nr_segs:%ul,count:%u,type:%d,iov_offset:%u", (*iter).nr_segs, iter->count, iter->type, iter->iov_offset);

	retval = episode_file_buffered_read(iocb, iter, retval); //7.我们一般采用这种方式读取数据到用户态的缓冲区
	
	//printk("---------------retval:%u--------------------------------", retval);
//	printk("after: type:%d,iov_offset:%u,before count:%u, count:%u,nr_segs:%lu,retval:%d", iter->type, iter->iov_offset, count, iov_iter_count(iter), iter->nr_segs, retval);
	//tmpage = __page_cache_alloc(mapping);
	//retnum = copy_page_from_iter_iovec(tmpage, 0, 4096,iter);
	//printk("after copy_page_from_iter_iovec, retnum:%u ",retnum);
  buff = (char*)kmalloc(4096,GFP_KERNEL);
	memset(buff,0,4096);
	retnum = copy_from_user(buff,ubuf,4096);
	//retnum = iov_iter_get_pages_alloc(iter, &pagevec,4096, &start);
	//memcpy_from_page(buff, &pagevec[0][0],0,4096);
printk("copy_from_user:%u", retnum);
	for (tmp = 0; tmp < 1024;)
	{
		printk("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ", 
						buff[tmp + 0], buff[tmp + 1], buff[tmp + 2], buff[tmp + 3], buff[tmp + 4], buff[tmp + 5], 
						buff[tmp + 6], buff[tmp + 7], buff[tmp + 8], buff[tmp + 9], buff[tmp + 10], buff[tmp + 11], 
						buff[tmp + 12], buff[tmp + 13], buff[tmp + 14], buff[tmp + 15]);
		tmp = tmp + 16;
	}
		buff2 = (char*) kmalloc(4096,GFP_KERNEL);
		memset(buff2,0,4096);
		curPos =0;
		while(curPos < 4096){
			 memcpy(&prev,buff+curPos,sizeof(prev));
			memcpy(&next,buff+curPos+sizeof(prev),sizeof(next));
			memcpy(&time,buff+curPos+sizeof(prev)+sizeof(next),sizeof(time));
			memcpy(&cur,buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time),sizeof(cur));
			memcpy(&recLen,buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur),sizeof(recLen));
			printk("prev:%ul, next:%ul, time:%u, cur:%ul, recLen:%u, buff+curPos:%p",prev,next,time,cur, recLen,buff+curPos);
			memcpy(buff2+curPos2, buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur), sizeof(recLen)+recLen);
			curPos2 = curPos2 + sizeof(recLen) +recLen;
			recNum ++;
			if(recNum == 1){
				base = cur;
			}
			if(next - base > 4096) break;
			curPos =  curPos  +sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur) +sizeof(recLen) + recLen;
			if(curPos != next - base - sizeof(prev)) {
				curPos = next- base - sizeof(prev);
			}
			
		}
		printk("curPos2:%u",curPos2);
		for( i=0;i<curPos2;){
printk("buff2[%d]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
i,buff2[i],buff2[i+1],buff2[i+2],buff2[i+3],buff2[i+4],buff2[i+5],buff2[i+6],buff2[i+7],buff2[i+8],buff2[i+9],buff2[i+10],buff2[i+11],buff2[i+12],buff2[i+13],buff2[i+14],buff2[i+15]);
		i =i+16;
		} 
	copy_to_user(ubuf,buff2,4096);
	
	/*put_page(tmpage);
	//在这里调用memcpy_to_page怎么样？

	buf = (char *)kmalloc(retval, GFP_KERNEL);
	if (!buf)
		printk("buf alloc mem failed!");
	memset(buf, 0, 4096);
	printk("iovec-> iov_len:%u, iov_base:%p", iter->iov->iov_len, iter->iov->iov_base);
	*/
//retnum = copy_from_user(buf,(iter->iov)[0].iov_base,1024);
	//retnum = copy_from_iter(buf, (size_t)retval, iter);//用于将iter 的数据传到用户态的空间，所以这个buf的地址不对
	//retnum = copy_from_iter_iovec_to_kernel_buf(buf, 1024, iter);//可以复制内容，但内容不正确
	//retnum = memcpy_from_iovec(buf, iter->iov, 1024);
//	retnum = copyin(buf,iter->iov->iov_base,1024);

/*	printk("not copyed bytes:%u", retnum);
	for (tmp = 0; tmp < 1024;)
	{
		printk("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ", buf[tmp + 0], buf[tmp + 1], buf[tmp + 2], buf[tmp + 3], buf[tmp + 4], buf[tmp + 5], buf[tmp + 6], buf[tmp + 7], buf[tmp + 8], buf[tmp + 9], buf[tmp + 10], buf[tmp + 11], buf[tmp + 12], buf[tmp + 13], buf[tmp + 14], buf[tmp + 15]);
		tmp = tmp + 16;
	}
	//kfree(buf);*/

	//struct iovec iov = iov_iter_iovec(iter);
	//retnum = iov_length(iter->iov, 1);
	printk("iov.iov_len:%ul,iov.iov_base:%x", (struct iovec *)(iter->iov[0]).iov_len, (struct iovec *)(iter->iov[0]).iov_base);
	//printk("iov.iov_len:%ul,iov.iov_base:%x, iov_length:%u",iov.iov_len,iov.iov_base,retnum);//这种用法失效
	//retnum = copy_from_iter(buf,4096,iter);//can not copy any byte!!!copy_from_iter只能将iter中的数据copy到用户空间的地址，不能copy到内核空间的地址;
	//printk("not copied bytes:%u",retnum);
	/*for (tmp = 0; tmp < 4096;)
	{
		printk("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ", buf[tmp + 0], buf[tmp + 1], buf[tmp + 2], buf[tmp + 3], buf[tmp + 4], buf[tmp + 5], buf[tmp + 6], buf[tmp + 7], buf[tmp + 8], buf[tmp + 9], buf[tmp + 10], buf[tmp + 11], buf[tmp + 12], buf[tmp + 13], buf[tmp + 14], buf[tmp + 15]);
		tmp = tmp + 16;
	}*/
	//kfree(buf);
	//kfree(buff);
	//iov_iter_get_pages(iter, pages, 4096, 1, 0);

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
	struct file *filp = iocb->ki_filp;							 //从iocb中拿到文件指针，
	struct address_space *mapping = filp->f_mapping; //获得文件对应的address_space对象，其实就是inode对应的address_space对象。
	struct inode *inode = mapping->host;						 //该address_space 对象mapping对应的inode
	struct file_ra_state *ra = &filp->f_ra;					 //文件预读相关的
	loff_t *ppos = &iocb->ki_pos;										 //long long 类型
	pgoff_t index;																	 //unsigned long类型，该页描述结构在地址空间radix树page_tree中的对象索引号即页号, 表示该页在vm_file中的偏移页数
	pgoff_t last_index;
	pgoff_t prev_index;
	unsigned long offset; /* offset into pagecache page */
	unsigned int prev_offset;
	int error = 0;
	char * buff;
	char *buff2;
	unsigned int curPos = 0,curPos2 = 0;//buff和buff2中的游标
	uint64_t prev,next,cur,base;
	uint32_t time,recLen,recNum = 0;

int i;

	if (unlikely(*ppos >= inode->i_sb->s_maxbytes)) //超出最大文件长度
		return 0;
	iov_iter_truncate(iter, inode->i_sb->s_maxbytes); //什么也没做。目的是确认iter->count是个正常的值，不会超长

	index = *ppos >> PAGE_SHIFT;									//计算ppos对应的位置应该在本文件的第几个页缓存中。相当于*ppos/4096,注意，这里的页缓存编号从0开始
	prev_index = ra->prev_pos >> PAGE_SHIFT;			//上次预读的起始页面id
	prev_offset = ra->prev_pos & (PAGE_SIZE - 1); //上次预读的页内偏移量
	//上面这两个变量可能会连同本次读的page一起用于更高层次的函数，判断使用哪种预读策略。注意：预读指的是从磁盘上预先读取数据放到缓存中，别搞错了
	last_index = (*ppos + iter->count + PAGE_SIZE - 1) >> PAGE_SHIFT; //计算这次读，会涉及到的最后一个页的id
	offset = *ppos & ~PAGE_MASK;																			//和index配合，ppos所在页面的偏移量。个人认为，有了index+offset可以确定读的起始位置，last_index确定要读到的结束页面
	//至于预读干啥用，还不清楚啊，
	//若PAGE_SIZE为4096，则PAGE_MASK为4095，二进制为000000000000000000000000111111111111

	for (;;)
	{
		struct page *page;
		pgoff_t end_index;
		loff_t isize;
		unsigned long nr, ret;

		cond_resched();
	find_page:
		if (fatal_signal_pending(current))
		{
			error = -EINTR;
			goto out;
		}

		page = find_get_page(mapping, index); /*在radix树中查找相应的page*/
		if (!page)
		{
			/* 如果没有找到page，内存中没有将数据,先进行预读，从磁盘上预读一定量的数据到页缓存中 */
			if (iocb->ki_flags & IOCB_NOWAIT) //阻塞
				goto would_block;
			//发出一个同步预读请求。预读机制很大程度上能够保证数据已经进入缓存
			page_cache_sync_readahead(mapping, ra, filp, index, last_index - index);
			page = find_get_page(mapping, index); //再次查找需要的页是否已经在内存缓存中
			if (unlikely(page == NULL))						//如果还是没有找到，则跳到no_cached_page，直接进行读取操作
				goto no_cached_page;
		}
		//
		if (PageReadahead(page))
		{ //判断page是否设置了PageReadahead标记，如果设置了，说明这次读的数据是上次读的后续，两次读是顺序的，因此可以进一步多读一些，此时则内核会采用更加激进的异步预读。
			//既然已经在缓存中找到了page，为啥还需要进一步异步预读呢？
			page_cache_async_readahead(mapping, ra, filp, page, index, last_index - index);
		}
		if (!PageUptodate(page))
		{ //检查page是否是最新的
			//这里是处理页不是最新的逻辑
			if (iocb->ki_flags & IOCB_NOWAIT)
			{
				put_page(page);
				goto would_block;
			}

			/*
			 * See comment in do_read_cache_page on why
			 * wait_on_page_locked is used to avoid unnecessarily
			 * serialisations and why it's safe.
			 */
			error = wait_on_page_locked_killable(page);
			//wait_on_page_locked_killable 的阻塞一般发生在 do_page_fault 里, 也就是在应用程序访问虚拟内存时发生的, 众所周知, do_page_fault 是在用户空间进程访问没有实际物理内存的虚拟内存时, 产生了缺页中断, 然后陷入到内核空间执行的函数.
			//https://blog.csdn.net/qkhhyga2016/article/details/79540119
			if (unlikely(error)) //error不是0，则if成立
				goto readpage_error;
			if (PageUptodate(page)) //现在是最新的页了
				goto page_ok;
			//页仍然不是最新的
			if (inode->i_blkbits == PAGE_SHIFT ||
					!mapping->a_ops->is_partially_uptodate) //我们没有设定这个函数
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
	page_ok: /* 数据内容是最新的 */
		/*下面这段代码是在page中的内容ok的情况下将page中的内容拷贝到用户空间去，主要的逻辑分为检查参数是否合法进性拷贝操作*/
		/*合法性检查*/
		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_SHIFT;
		//	printk("end_index:%d,index:%d,isize:%ld， unlikely(!isize || index > end_index)：%d,(!isize || index > end_index):%d,unlikely(0):%d,unlikely(1):%d,likely(1):%d,likely(2):%d,likely(0):%d\n",end_index,index,isize,unlikely(!isize || index > end_index),(!isize || index > end_index),unlikely(0),unlikely(1),likely(1),likely(2),likely(0));
		if (unlikely(!isize || index > end_index))
		{																//unlikely(true) =true; unlikely(false)=false;
			printk("I am here!!!!!!!\n"); //这句不会执行
			put_page(page);
			goto out;
		}

		/* nr是能从本page中copy的最大字节数，显然就是Page_size了 */
		nr = PAGE_SIZE;
		if (index == end_index)
		{																			 //最后一個页面，此时还没有写满，是有可能的。
			nr = ((isize - 1) & ~PAGE_MASK) + 1; //最后一个页里面所含有的字节数
			if (nr <= offset)
			{ //出错了
				put_page(page);
				goto out;
			}
		}
		nr = nr - offset; //要读取的字节数？

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
		 * 到此为止，我们已经拿到了所需要的pages，并且它们都是最新的,下一步，就可以把这些pages从内核空间复制到用户空间了。
		 */
		//printk("iter->nsegs:%u,iter->count:%u,iov_base:%p,offset:%d,nr:%d",iter->nr_segs,iter->count,iter->iov->iov_base,offset,nr);
	//printk("before copy page to iter, offset:%u, nr:%u",offset, nr);
		ret = copy_page_to_iter(page, offset, nr, iter);//如果后面的逻辑能处理好，是否是先将page中的内容复制到buff中，然后读取buff进行处理，结果放到buff2中，然后复制到iter中？
		//printk("after copy page to iter, offset:%u, nr:%u, ret:%u",offset, nr, ret);
		//将页缓存中的内容copy到用户态的缓冲区(iter的iov指针对象），如果修改这边，则需要重新定义copy_page_to_iter()，然后不是一次将多个页面直接copy过来，
		//而是按照记录的粒度在内核态内部进行处理，但是涉及到page重组等问题，其实可能更麻烦。比较容易处理的方式，是在episode_file_read_iter中
		//对返回的iter和iovec进行处理。我们暂时就采用这种方式。
/*printk("iter->nsegs:%u,iter->count:%u,iov_base:%p,offset:%d,nr:%d",iter->nr_segs,iter->count,iter->iov->iov_base,offset,nr);
		buff = (char*)kmalloc(4096,GFP_KERNEL);
		memset(buff,0,4096);
memcpy_from_page(buff, page,offset,nr);
		for( i=0;i<120;i++) printk("buff[%d]:%d",i,buff[i]);
//从buff中去掉index信息，放到buff2中		
		buff2 = (char*) kmalloc(4096,GFP_KERNEL);
		memset(buff2,0,4096);
		curPos =0;
		while(curPos < 4096){
			 memcpy(&prev,buff+curPos,sizeof(prev));
			memcpy(&next,buff+curPos+sizeof(prev),sizeof(next));
			memcpy(&time,buff+curPos+sizeof(prev)+sizeof(next),sizeof(time));
			memcpy(&cur,buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time),sizeof(cur));
			memcpy(&recLen,buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur),sizeof(recLen));
			printk("prev:%ul, next:%ul, time:%u, cur:%ul, recLen:%u, buff+curPos:%p",prev,next,time,cur, recLen,buff+curPos);
			memcpy(buff2+curPos2, buff+curPos+sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur), sizeof(recLen)+recLen);
			curPos2 = curPos2 + sizeof(recLen) +recLen;
			recNum ++;
			if(recNum == 1){
				base = cur;
			}
			if(next - base > 4096) break;
			curPos =  curPos  +sizeof(prev)+sizeof(next)+sizeof(time)+sizeof(cur) +sizeof(recLen) + recLen;
			if(curPos != next - base - sizeof(prev)) {
				curPos = next- base - sizeof(prev);
			}
			
		}
		printk("curPos2:%u",curPos2);
		for( i=0;i<curPos2;){
printk("buff2[%d]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
i,buff2[i],buff2[i+1],buff2[i+2],buff2[i+3],buff2[i+4],buff2[i+5],buff2[i+6],buff2[i+7],buff2[i+8],buff2[i+9],buff2[i+10],buff2[i+11],buff2[i+12],buff2[i+13],buff2[i+14],buff2[i+15]);
		i =i+16;
		} 
	  memcpy_to_page(page,offset,buff2,nr);
		ret = copy_page_to_iter(page, offset, nr, iter);
*/
		offset += ret;
	//	printk("offset:%u",offset);
		index += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;
		prev_offset = offset;
	//printk("after copy page to iter and update, offset:%u, nr:%u, index:%u",offset, nr, index);
		put_page(page);
		written += ret;
		if (!iov_iter_count(iter))
			goto out;
		if (ret < nr)
		{
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
		if (!page->mapping)
		{
			unlock_page(page);
			put_page(page);
			continue;
		}

		/* Did somebody else fill it already? */
		if (PageUptodate(page))
		{
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

		if (unlikely(error))
		{
			if (error == AOP_TRUNCATED_PAGE)
			{
				put_page(page);
				error = 0;
				goto find_page;
			}
			goto readpage_error;
		}

		if (!PageUptodate(page))
		{
			error = lock_page_killable(page);
			if (unlikely(error))
				goto readpage_error;
			if (!PageUptodate(page))
			{
				if (page->mapping == NULL)
				{
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
		page = page_cache_alloc(mapping); //在缓存中申请一个页
		if (!page)
		{ //内存分配失败
			error = -ENOMEM;
			goto out;
		}
		error = add_to_page_cache_lru(page, mapping, index,
																	mapping_gfp_constraint(mapping, GFP_KERNEL)); //将新申请的页挂到缓存区的链表（LRU表)中
		if (error)
		{
			put_page(page); //释放页
			if (error == -EEXIST)
			{ //如果在将新页挂到链表时发现这个page已经存在了（其他操作已经挂上去了），则这个不用挂了，直接返回到find_page
				error = 0;
				goto find_page;
			}
			goto out;
		}
		goto readpage; //挂链完成，下面就可以读page的内容啦
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

static size_t memcpy_from_iovec(char *kdata, struct iovec *iov, size_t len)
{
	unsigned long iovlen = iov->iov_len;
	int i, j,retnum;
	printk("iov_len:%ul,iov_base:%p", iovlen, iov->iov_base);
	char __user *buf = iov->iov_base;
	char * temp;
	char * tmp;
	temp = (char*)kmalloc(1024,GFP_KERNEL);
	tmp = (char*)kmalloc(1024,GFP_KERNEL);
	printk("buf:%p,temp:%p,tmp1:%p", buf,temp,tmp);
	memset(temp,0,1024);
	memcpy(temp,buf,1024);
	memset(tmp,0,1024);
	retnum = copy_from_user(tmp,buf,1024);
	printk("retnum:%d",retnum);

	for(i=0;i<1024;i++) printk("temp=%d tmp1=%d",temp[i],tmp[i]);
	//for(i=0;i<1024;i++) printk("tmp1=%d ",tmp[i]);
	kfree(temp);
	kfree(tmp);
	j = copy_from_user(kdata, buf, len);
	printk("copy left:%d", j);
	if (j)
	{
		for (i = 0; i < len; i++)
		{
			printk("aaa=%02x ", kdata[i]);
		}
		printk("copy from user error！");
		return -EFAULT;
	}
	//iov->iov_base = buf;
	return 0;
}
static size_t copy_from_iter_iovec_to_kernel_buf(void *to, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	printk("I am in [copy_from_iter_iovec_to_kernel_buf]! iter->count :%u, iov_base:%p", (i->iov[0]).iov_len,i->iov->iov_base);
	left = __copy_from_user(to, (i->iov)->iov_base, 1024);

	printk("left:%u,iov_base:%p", left,i->iov->iov_base);
}
static size_t copy_from_iter_iovec1(void *to, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	printk("I am here 1! iter->count :l%u", (i->iov[0]).iov_len);

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;
	printk("I am here 2!");
	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	printk("iov_len:%lu", iov->iov_len);
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	left = __copy_from_user(to, buf, copy);
	printk("copy:%u,left:%u", copy, left);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes))
	{
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_from_user(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}

	if (skip == iov->iov_len)
	{
		iov++;
		skip = 0;
	}
	//最终对iter 赋值的地方，这里可能需要修改
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	printk("at the end, i->count:%u, i->nr_segs:%u,i->iov_offset:%u",i->count, i->nr_segs, i->iov_offset);
	return wanted - bytes;
}


static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *to;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	might_fault();
	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_pages_readable(buf, copy)) {
		kaddr = kmap_atomic(page);
		to = kaddr + offset;

		/* first chunk, usually the only one */
		left = copyin(to, buf, copy);
		copy -= left;
		skip += copy;
		to += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = copyin(to, buf, copy);
			copy -= left;
			skip = copy;
			to += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = to - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */

	kaddr = kmap(page);
	to = kaddr + offset;
	left = copyin(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = copyin(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}
	kunmap(page);

done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}


/*
 * We have mostly NULLs here: the current defaults are OK for
 * the episode filesystem.
 */
const struct file_operations episode_file_operations = {
		.llseek = generic_file_llseek,
	//	 .read_iter	= generic_file_read_iter,
		.read_iter = episode_file_read_iter,
		.write = episode_direct_write,
		.mmap = generic_file_mmap,
		.fsync = generic_file_fsync,
		.splice_read = generic_file_splice_read,
};

const struct inode_operations episode_file_inode_operations = {
		.setattr = episode_setattr,
		.getattr = episode_getattr,
};
