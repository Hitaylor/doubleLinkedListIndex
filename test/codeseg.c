
static ssize_t episode_direct_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	//读取inode中的上一条记录的位置，jsc 0510
        __u64 lastRecPos = 0;
        struct episode_inode * raw_inode;
        struct inode * inode = file_inode(filp);
	struct episode_inode_info *episode_inode = episode_i(inode);
        lastRecPos = episode_inode->i_lastrecordpos;
        __u64 curPos = inode->i_size;
        //构造新的buff，然后遍历buf，创建新的记录，填充到buff中。jsc 0510
        char * buff = NULL;
        __u32 pos = 0, recLen = 0, tmp= 0,len = 0, timestamp = 0,position=0;
        __u64 prev = 0,next = 0,offset = 0;
        char * lenSeg,time;
        char * ptr8;
        while(pos < len-1){ 
           mid(lenSeg, buf, pos, pos+4);
           recLen = atoi(lenSeg);
           //构造索引结构和索引信息
           //prev,next,timestamp,offset,len,data
           prev = lastRecPos;
           next = curPos+8+16+recLen;
           timestamp = getCurrentTime();
           offset = curPos;
           len = recLen;
           //先用itoa，再使用memcpy？
           itoa(prev,ptr8,10);
           memcpy(&buff[position],ptr8,8);
           position = position +8;
           itoa(next,ptr8,10);
           memcpy(&buff[position],ptr8,8);
           position = position +8;
           itoa(timestamp,time,10);
           memcpy(&buff[position],time,4);
           position = position +4;
           itoa(curPos,ptr8,10);
           memcpy(&buff[position],ptr8,8);
           position = position +8;
           memcpy(&buff[position],lenSeg,4);
           position = position +4;
           memcpy(&buff[position],&buf[4],len-4);
           position = position +len-4;
           pos = pos+recLen;
        }
        //修改buf，jsc
       // struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
        struct iovec iov = { .iov_base = (void __user *)buff, .iov_len = position+1 };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;
        

	init_kiocb(&kiocb, filp);
	kiocb.ki_pos = *ppos;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = generic_file_write_iter(&kiocb, &iter);

        BUG_ON(ret == -EIOCBQUEUED);
        if (ret > 0)
                *ppos = kiocb.ki_pos;
        return ret;
}