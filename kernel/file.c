#include "episode.h"
#include <uapi/linux/uio.h>
#include <linux/uio.h>
#include <linux/slab.h>

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
        char * buff = NULL;
       
        __u32 pos = 0;//buf内的偏移量
        __u32  dataSizeinRec = 0;//buf内当前数据记录的数据段长度
        __u32 tmp= 0;
        __u32 bufLen = 0;//buf的长度
        __u32 timestamp = 0;//时间戳
        __u32 position=0;
        __u64 prev = 0,next = 0,offset = 0;//在文件中，当前记录的前一个记录的起始位置、next字段起始位置，以及当前记录的数据段位置
        char  lenSeg[sizeof(dataSizeinRec)],time[sizeof(timestamp)];//用于记录长度和时间的临时变量
        char * ptr8;
        int i;
        __u64 curPos;
        __u64 lastRecPos = 0;
        
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
        curPos = inode->i_size;//文件游标位置
        printk("The last data record position: %ld\n current postion:%ld\n",lastRecPos,curPos);
        if(len%512 != 0)
        {
           printk("user buf len %lu mod 512!=0, now return -1! \n",len);
           return -1;
        }
        //这里，后续可以先读取一遍buf，得到具体的record数量n，因为对于每条record，扩展需要添加的字节数是固定的，8+8+8+4=28字节，则比原来需要增加28n字节，然后将len+28n向上取512的整数倍，即为buff的长度
       // buff = memalign(512,(1+len/512)*512);
        buff = kmalloc((1+len/512)*sizeof(char), GFP_KERNEL);
        if(!buff)
        {
           printk("kmalloc failed for the buff!\n");
        }
        printk("buff size %d\n",sizeof(buff));

        while(pos < bufLen-1){ //遍历buf中的每一条记录，进行扩充，形成新的结构，然后放到buff中。
           mid(lenSeg, buf, pos, pos+sizeof(dataSizeinRec));//获取buf中一条记录的长度字段
           //dataSizeinRec = atoi(lenSeg);//不能使用atoi转换成数字长度，因为它是将“12345”转成12345，前面的"12345“占5位，而int实际上只占4位，在buf中应该是12345的4字节的二进制表示
           if(!lenSeg){
               printk("lenSeg parse failed!\n"); 
           }
           memcpy(&dataSizeinRec,lenSeg,sizeof(dataSizeinRec));//将lenSeg表达的内容强制转换成int
           printk("data size in the record is %d\n",dataSizeinRec);
           //构造索引结构和索引信息
           //prev,next,timestamp,offset,len,data
           prev = lastRecPos;
           next = curPos+sizeof(prev)+sizeof(next)+sizeof(timestamp)+sizeof(offset)+sizeof(dataSizeinRec)+dataSizeinRec+sizeof(prev);
           timestamp = getCurrentTime();
           offset = curPos;
           len = dataSizeinRec;
           //先用itoa，再使用memcpy？
           //itoa(prev,ptr8,10);
           memcpy(&buff[position],&prev,sizeof(prev));
           position = position +sizeof(prev);
           //ptr8 = NULL;
           
           //itoa(next,ptr8,10);
           memcpy(&buff[position],&next,sizeof(next));
           position = position +sizeof(next);
           //ptr8 = NULL;
           
           //itoa(timestamp,time,10);
           memcpy(&buff[position],&timestamp,sizeof(timestamp));
           position = position +sizeof(timestamp);
           
           //itoa(curPos,ptr8,10);
           memcpy(&buff[position],&curPos,sizeof(curPos));
           position = position +sizeof(curPos);
           //ptr8 = NULL;
           /*
           memcpy(&buff[position],lenSeg,sizeof(dataSizeinRec));
           position = position +4;
           memcpy(&buff[position],&buf[4],len-4);
           position = position +len-4;
           */
          //copy buf中该数据记录到buff中,替换上面的两次memcpy()调用
          memcpy(&buff[position],&buf[pos],sizeof(dataSizeinRec)+dataSizeinRec);

           pos = pos+sizeof(dataSizeinRec)+dataSizeinRec;
           printk("current pos:%d\n",pos);
        }
        printk("After extending, data in buff are:\n");
        for(i=0;i<pos;i++){
                printk("%c",buff[i]);
        }
        printk("\n");
        

        //修改buf，jsc
       // struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
        iov.iov_base = (void __user *)buff;
        iov.iov_len = position+1;//此时buff的长度应为512整数倍。但这里的position+1却不一定，所以这里填写什么需要确认一下
	
        

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
