#include "episode.h"
#include <uapi/linux/uio.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
//#include "itree.h"

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
 * 这里需要注意的是：buf的大小应该是512的2^n的倍数。也就是说，buf中前面的数据是没有填充的，之后最后一条数据的后面的填充的。所以，我们在构造buff的时候
 * 也要注意处理这方面的内容。也就是buf中有一部分内容是空的。如果我们构造的buff不是512的2^n倍数，则可能写不进去。
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
        __u32 bufLen = len;//buf的长度
        __u32 timestamp = 0;//时间戳
        __u64 position=0;//与prev、next类型一致
        __u64 prev = 0,next = 0,offset = 0;//在文件中，当前记录的前一个记录的起始位置、next字段起始位置，以及当前记录的数据段位置
        char  lenSeg[4]={0},time[sizeof(timestamp)]={0};//用于记录长度和时间的临时变量
        __u32  recLen=0;
        char * ptr8;
        int i,testcount=0;
        __u64 tmpLen;
        __u64 curPos;
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
        printk("I am here! and the user buf size is %d\n" ,len);
        lastRecPos = episode_inode->i_lastrecordpos;
        tmpLen = i_lastrecordpos(inode);
        curPos = inode->i_size;//文件游标位置,也是这次写操作的base
        printk("The last data record position: %ld\t tmpLen=%lld\n current postion:%ld\n",lastRecPos,tmpLen,curPos);
        if(len%512 != 0)
        {
           printk("user buf len %lu mod 512!=0, now return -1! \n",len);
           return -1;
        }
        printk("len=%d\n",len);
        //这里，后续可以先读取一遍buf，得到具体的record数量n，因为对于每条record，扩展需要添加的字节数是固定的，8+8+8+4=28字节，则比原来需要增加28n字节，然后将len+28n向上取512的整数倍，即为buff的长度
       // buff = memalign(512,(1+len/512)*512);
        buff =(char *) kmalloc((1+len/512)*512, GFP_KERNEL);
        //printk("ater kmalloc, buff:");
       // for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
        memset(buff,0,(1+len/512)*512);
        // printk("ater memset, buff:");
        //for(i=0;i<1024;i++) printk("buff[%d]=%u",i,buff[i]<0?(255+buff[i]):buff[i]);
        if(!buff)
        {
           printk("kmalloc failed for the buff!\n");
        }
        printk("buff size %d\n",sizeof(buff));
        printk("I am here 2!\n");
        buff2 = (char *)kmalloc((1+len/512)*512, GFP_KERNEL);
        memset(buff2,0,(1+len/512)*512);
        
        printk("size of buff2:%d\n",sizeof(buff2));
        retnum = copy_from_user(buff,buf,1024);
        //printk("ater copy from user, buff:");
        //for(i=0; i<len; i++) printk("buff[%d]=%d, ",i,buff[i]);
        printk(" I am here 3! retnum=%d for the function copy_from_user(). pos=%d, bufLen=%d\n",retnum,pos,bufLen);
        //这里bufLen=0,是有问题的
        while(pos < bufLen-1){ //遍历buf中的每一条记录，进行扩充，形成新的结构，然后放到buff中。
          // mid_char(&lenSeg[0], buf, 4, pos);//获取buf中一条记录的长度字段
          // printk("Address of reLen : %x, buff:%x\n",&recLen,buff);
           memcpy(&recLen,&buff[pos],sizeof(recLen));
           printk(" I am here 4! and recLen=%d\n",recLen);
           if(recLen == 0) break;//跳出while，也就是buf中已经没有新记录了。
          
           //构造索引结构和索引信息
           //prev,next,timestamp,offset,len,data
           prev = lastRecPos;
           next = curPos+sizeof(prev)+sizeof(next)+sizeof(timestamp)+sizeof(offset)+sizeof(recLen)+recLen+sizeof(prev);
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
        retnum = clear_user(buf,len);
        printk("bytes can not be cleared in the user buf is %d/%d\n",retnum,len);
        retnum = copy_to_user(buf,buff2,len);//改为按照buf的大小来赋值，将来可以通过循环控制，放置buf大小不能更改
        printk("bytes cannot be copied to user space is retnum=%d for the function copty_to_user()\n",retnum);
        //再次将buf中的内容copy到buff中，测试buf中是否有内容
       
         printk("pos = %d\n",pos);
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
        //修改buf，jsc
       // struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
        iov.iov_base = (void __user *)buf;
        //iov.iov_len = position+1;//此时buff的长度应为512整数倍。但这里的position+1却不一定，所以这里填写什么需要确认一下
	iov.iov_len = len;
        

	init_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

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

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the episode filesystem.
 */
const struct file_operations episode_file_operations = {
    .llseek	= generic_file_llseek,
    .read_iter	= generic_file_read_iter,
    .write	= episode_direct_write,
    .mmap	= generic_file_mmap,
    .fsync		= generic_file_fsync,
    .splice_read	= generic_file_splice_read,
};

const struct inode_operations episode_file_inode_operations = {
	.setattr	= episode_setattr,
	.getattr	= episode_getattr,
};
