/*
 * @Author: 金松昌
 * @Date: 2020-04-21 15:04:32
 * @LastEditors: 金松昌
 * @LastEditTime: 2020-04-22 17:04:21
 * @Description: 
 */

//http://www.voidcn.com/article/p-vmcbttdj-sh.html

//1. open()系统调用-->2.do_sys_open()-->3. do_filp_open()-->4.path_openat()-->5.link_path_walk()和6 do_last()-->7.vfs_open()-->8.do_dentry_open()
//1
SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
{
	if (force_o_largefile())
		flags |= O_LARGEFILE;

	return do_sys_open(AT_FDCWD, filename, flags, mode);
}
 
//2
long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode)
{
	struct open_flags op;
	int fd = build_open_flags(flags, mode, &op);//2.1主要是用来构建flags，并返回到结构体 struct open_flags op中
	struct filename *tmp;

	if (fd)
		return fd;

	tmp = getname(filename);//2.2 /*获取文件名称，由getname()函数完成，其内部首先创建存取文件名称的空间，然后从用户空间把文件名拷贝过来*/
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	fd = get_unused_fd_flags(flags);
    //2.3获取一个可用的fd，此函数调用alloc_fd()函数从fd_table中获取一个可用fd,并做些简单初始化，此函数内部实现比较简单，https://blog.csdn.net/arm7star/article/details/78668951
	//新的文件描述符存放在sys_open函数的fd局部变量中,在current->files->fd（task_struct->file_struct->file指针数组）中查找一个空的位置（file结构体标识由进程打开的文件）。https://www.linuxidc.com/Linux/2013-01/77614.htm
    if (fd >= 0) {
		struct file *f = do_filp_open(dfd, tmp, &op);//3.主要完成打开功能的函数
		if (IS_ERR(f)) {
			put_unused_fd(fd);//和前面的get_unused_fd_flag相反，释放占用的fd
			fd = PTR_ERR(f);
		} else {
			fsnotify_open(f);//2.4/*文件如果已经被打开了，调用fsnotify_open()函数*/fsnotify其实是将文件系统变化通知这个特性进行了解耦合的架构，抽
            //象出了fsnotify_event和fsnotify_group数据结构，event代表了事件，可以是打开，读写，删除等等，它在具体事件发生时懒惰创建，而group代表了接收事件的
            //策略，用户每创建一个监控通知inode，就相当于创建了一个group，然后将文件系统中要被监控的文件加入到这个group的监控列表当中，一旦有事件发生，内核就遍
            //历所有的group，在每个group中遍历该group感兴趣的inode，一旦发现就在第一个group被发现后创建event，然后依次全部交由这些group的fsnotify_ops中的
            //handle_event来进行实际的通知，这个handle_event是一个回调函数，不同的通知系统可以实现不同的策略，在inotify中，它的实现就是创建一个事件的holder，
           // 然后将之将event加入到该group的一个链表中，注意，此时用户空间可能有进程睡眠在该inotify相关的文件上，体现为group的一个睡眠队列，待holder加
           //入到链表之后，最后唤醒睡眠队列上的进程。
           //简单地讲，这个句子就是告诉系统，文件f对应的inode被打开了，如果需要对inode做相应的处理，请接到通知后进行处理（比例inode中的引用计数要增加1）
			fd_install(fd, f);//2.5/*将文件指针安装在fd数组中*/通过查找fd数组，就可以找到fd对应的文件指针file。在内核中建立 fd 与 file 的对应关系，这样以后就可以通过fd来找到对应的file。

			trace_do_sys_open(tmp->name, flags, mode);//2.6 内核跟踪框架trace的内容，用于跟踪do_sys_open（）这个函数的执行过程
            //kernel中有trace_XX形式的函数，这些是kernel的tracepoint，定义在include/linux/tracepoint.h中。trace_要起作用，需要调用register_trace_##name，给他关联一个probe函数，
            //当调用trace_时就执行probe函数内核中的每个tracepoint提供一个钩子来调用probe函数。一个tracepoint可以打开或关闭。打开时，probe函数关联到tracepoint；关闭时，probe函数不关联到tracepoint。tracepoint关闭时对kernel产生的影响很小，只是增加了极少的时间开销（一个分支条件判断），极小的空间开销（一条函数调用语句和几个数据结构）。当一个tracepoint打开时，用户提供的probe函数在每次这个tracepoint执行是都会被调用。
		}
	}
	putname(tmp);
	return fd;
}
//2.1 主要是用来构建flags，并返回到结构体 struct open_flags op中,在该函数中，根据open的时候传入的参数进行设定，所以很多代码可能不执行
static inline int build_open_flags(int flags, umode_t mode, struct open_flags *op)
{
    int lookup_flags = 0;
    int acc_mode = ACC_MODE(flags);
 
    flags &= VALID_OPEN_FLAGS;
 
    if (flags & (O_CREAT | __O_TMPFILE)) /* if 判断为 0 */
        ...
    else
        op->mode = 0;
 
    /* Must never be set by userspace */
    flags &= ~FMODE_NONOTIFY & ~O_CLOEXEC;
 
    if (flags & __O_SYNC) /* if 判断为 0 */
        ...
 
    if (flags & __O_TMPFILE) { /* if 判断为 0 */
        ...
    } else if (flags & O_PATH) { /* if 判断为 0 */
       ...
    }
 
    op->open_flag = flags;
 
    if (flags & O_TRUNC) /* if 判断为 0 */
        ...
    if (flags & O_APPEND) /* if 判断为 0 */
        ...
 
    op->acc_mode = acc_mode;
    op->intent = flags & O_PATH ? 0 : LOOKUP_OPEN; /* op->intent = LOOKUP_OPEN */
 
    if (flags & O_CREAT) { /* if 判断为 0 */
        ...
    }
    if (flags & O_DIRECTORY) /* if 判断为 0 */
        ...
    if (!(flags & O_NOFOLLOW)) /* if 判断为 1 */
        lookup_flags |= LOOKUP_FOLLOW;
    op->lookup_flags = lookup_flags;
    
    /* 此时 op 各个成员变量值如下：
    * op->mode = 0
    * op->open_flag = 0x8000;
    * op->acc_mode = 0x4
    * op->intent = 0x100 (LOOKUP_OPEN)
    * op->lookup_flags = 0x1 (LOOKUP_FOLLOW)
    */
    return 0;
}

//2.2将用户空间的文件路径名拷贝到内核空间
struct filename * getname(const char __user * filename)
{
	return getname_flags(filename, 0, NULL);//2.2.1
}
filename结构体的原型：
struct filename {
	const char		*name;	/* pointer to actual string */
	const __user char	*uptr;	/* original userland pointer */
	struct audit_names	*aname;
	int			refcnt;
	const char		iname[];
};
//2.2.1
struct filename * getname_flags(const char __user *filename, int flags, int *empty)
{
	struct filename *result;
	char *kname;
	int len;

	result = audit_reusename(filename);/* 为文件名分配一个物理页面作为缓冲区，因为一个绝对路径可能很长，如果使用临时变量的话，这个路径就被存储在系统堆栈段中。返回值为filename结构体。*/
	if (result)
		return result;

	result = __getname();//实质上调用kmem_cache_alloc函数分配一个新的slab对象，在内核缓冲区专用队列里申请一块内存用来放置路径名，其实这块内存就是一个 4KB 的内存页
    //这块内存页是这样分配的，在开始的一小块空间放置结构体 struct filename，之后的空间放置字符串.
	if (unlikely(!result))
		return ERR_PTR(-ENOMEM);

	/*
	 * First, try to embed the struct filename inside the names_cache
	 * allocation
	 */
	kname = (char *)result->iname;//把kname所指向的地址接在result最后的那一部分（存储字符串的首地址）
	result->name = kname;

	len = strncpy_from_user(kname, filename, EMBEDDED_NAME_MAX);//执行从用户空间复制数据到内核空间，返回复制的字节数
	if (unlikely(len < 0)) {
		__putname(result);
		return ERR_PTR(len);
	}

	/*
	 * 路径长度接近Name_MAX时，将kname变量所指向的地址指向result所指向的地址，相当于增大了kname字符串所分配到的空间，之后再重新申请一个filename对象
	 */
	if (unlikely(len == EMBEDDED_NAME_MAX)) {//如果名字过长，filename结构体达到了4k（一个内存page大小），此时要将这个filename从内存页中分离出来，然后单独分配空间，然后用整个内存页来保存该字符串
		const size_t size = offsetof(struct filename, iname[1]);//获取iname之前的长度
		kname = (char *)result;

		/*
		 * size is chosen that way we to guarantee that
		 * result->iname[0] is within the same object and that
		 * kname can't be equal to result->iname, no matter what.
		 */
		result = kzalloc(size, GFP_KERNEL);//重新分配内存
		if (unlikely(!result)) {
			__putname(kname);
			return ERR_PTR(-ENOMEM);
		}
		result->name = kname;
		len = strncpy_from_user(kname, filename, PATH_MAX);//重新复制
		if (unlikely(len < 0)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(len);
		}
		if (unlikely(len == PATH_MAX)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(-ENAMETOOLONG);
		}
	}

	result->refcnt = 1;
	/* The empty path is special. */
	if (unlikely(!len)) {
		if (empty)
			*empty = 1;
		if (!(flags & LOOKUP_EMPTY)) {
			putname(result);
			return ERR_PTR(-ENOENT);
		}
	}

	result->uptr = filename;
	result->aname = NULL;
	audit_getname(result);
	return result;
}

//3 
struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op)
{
    /* 函数入口参数（实例）：
    * dfd = 100
    * pathname.name = "/home/ok/a.txt"
    * op 见 build_open_flags
    */ 
    struct nameidata nd;
	int flags = op->lookup_flags;
	struct file *filp;
 
	set_nameidata(&nd, dfd, pathname);//3.1 用来设置结构体struct nameidata的值，这个结构体是个非常重要的结构体，在解析和查找路径名时会经常引用到
	filp = path_openat(&nd, op, flags | LOOKUP_RCU);//4. rcu_walk效率高，期间禁止抢占，也不允许进程阻塞
	if (unlikely(filp == ERR_PTR(-ECHILD)))
		filp = path_openat(&nd, op, flags);//4一种ref_walk，效率较低
	if (unlikely(filp == ERR_PTR(-ESTALE)))
		filp = path_openat(&nd, op, flags | LOOKUP_REVAL);//4，其实也是一种ref_walk内核的文件系统是nfs的时候才会用到这个模式。该模式是在已经完成了路径查找，打开具体文件时，如果该文件已经过期（stale）才启动的，所以 REVAL 是给具体文件系统自己去解释的
	restore_nameidata();
	return filp;
}
//3.1 对nameidata进行了一些初始化赋值，将要查询的文件名放入了结构体nameidata中，并且将从current取出的nameidata保存下来，组成了一个链表。
//path对应的是全路径的结构还是只对着路径中的某个目录名或者文件名呢？
struct path {
	struct vfsmount *mnt;//指向文件系统装载对象的指针
	struct dentry *dentry;//指向目录项对象的指针
};

//vfsmount结构描述的是一个独立文件系统的挂载信息，每个不同挂载点对应一个独立的vfsmount结构，属于同一文件系统的所有目录和文件隶属于同一个vfsmount，该vfsmount结构对应于该文件系统顶层目录，即挂载目录
//比如对于mount /dev/sdb1 /media/Kingston,挂载点为/media/Kingston，对于Kingston这个目录，其产生新的vfsmount，独立于根文件系统挂载点/所在的vfsmount；
//所有的vfsmount挂载点通过mnt_list双链表挂载于mnt_namespace->list链表中，该mnt命名空间可以通过任意进程获得
//子vfsmount挂载点结构通过mnt_mounts挂载于父vfsmount的mnt_child链表中，并且mnt_parent直接指向父亲fs的vfsmount结构，从而形成层次结构
//对于挂载点/media/Kingston来讲，其vfsmount->mnt_root->f_dentry->d_name.name = ‘/’;而vfsmount->mnt_mountpoint->f_dentry->d_name.name = ‘Kingston’。对于/media/Kingston下的所有目录和文件而言，都是这样的。

struct vfsmount {
 	 struct list_head mnt_hash;
  	 struct vfsmount *mnt_parent; /* fs we are mounted on */     
	 struct dentry *mnt_mountpoint; /* dentry of mountpoint */
	 struct dentry *mnt_root; /* root of the mounted tree */
	 struct super_block *mnt_sb; /* pointer to superblock */
	 struct list_head mnt_mounts; /* list of children, anchored here */
	 struct list_head mnt_child; /* and going through their mnt_child */
	 int mnt_flags;
	 /* 4 bytes hole on 64bits arches */
	 const char *mnt_devname; /* Name of device e.g. /dev/dsk/hda1 */
	 struct list_head mnt_list;
	 struct list_head mnt_expire; /* link in fs-specific expiry list */
	 struct list_head mnt_share; /* circular list of shared mounts */
	 struct list_head mnt_slave_list;/* list of slave mounts */
	 struct list_head mnt_slave; /* slave list entry */
	 struct vfsmount *mnt_master; /* slave is on master->mnt_slave_list */
	 struct mnt_namespace *mnt_ns; /* containing namespace */
	 atomic_t __mnt_writers;
	
};



struct nameidata {
	struct path	path;//保存当前搜索到的路径
	struct qstr	last;//保存当前自路径名及其散列值
	struct path	root;//查询的根路径
	struct inode	*inode; /* path.dentry.d_inode *///指向当前找到的目录项的inode结构；
	unsigned int	flags;//和查找相关的标志位
	unsigned	seq, m_seq;//相关目录项的顺序锁序号，和相关文件系统（其实是mount）的顺序锁序号
	int		last_type;//当前子路径的类型（5种，LAST_ROOT 是 “/”；LAST_DOT 和 LAST_DOTDOT 分别代表了 “.” 和 “..”；LAST_BIND 就是符号链接；LAST_NORM）
	unsigned	depth;//在解析符号链接过程中的递归深度
	int		total_link_count;//用来记录符号链接的深度，每穿越一次符号链接这个值就加一，最大允许 40 层符号链接
	struct saved {
		struct path link;
		struct delayed_call done;
		const char *name;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename	*name;
	struct nameidata *saved;//记录相应递归深度的符号链接的路径
	struct inode	*link_inode;
	unsigned	root_seq;
} __randomize_layout;



static void set_nameidata(struct nameidata *p, int dfd, struct filename *name)
{
    struct nameidata *old = current->nameidata;//首先使用局部指针指向当前进程的nameidata，也就是每个进程都包含一个nameidata结构
    p->stack = p->internal;
    p->dfd = dfd;//把dfd放到了p中，
    p->name = name; //把filename也放到了p中，这样实现了nameidata与文件以及fd的关联。
    p->total_link_count = old ? old->total_link_count : 0;//如果current->nameidata不是null，则用它的，否则为0，据说是用来防止循环死链的
    p->saved = old;
    current->nameidata = p;
}
//4.主要作用是首先为 struct file 申请内存空间，设置遍历路径的初始状态，然后遍历路径并找到最终目标的父节点，最后根据目标的类型和标志位完成 open 操作，最终返回一个新的 file 结构
static struct file *path_openat(struct nameidata *nd,
            const struct open_flags *op, unsigned flags)
{
    /* 函数入口参数： flags = LOOKUP_FOLLOW | LOOKUP_RCU */
    const char *s;
    struct file *file;
    int opened = 0;
    int error;
 
    file = get_empty_filp();//为struct file 分配内存，获得一个未使用的文件缓存空间即file结构体,4.1
    if (IS_ERR(file)) /* if 判断为 0 */
        ...
 
    file->f_flags = op->open_flag; /* f_flags == O_LARGEFILE */
 
    if (unlikely(file->f_flags & __O_TMPFILE)) { /* if 判断为 0 */
        ...
    }
    if (unlikely(file->f_flags & O_PATH)) { /* if 判断为 0 */
        ...
    }
 /* 初始化检索的起始目录，判断起始目录是根目录还是当前目录，并且初始化nd->inode对象，为link_path_walk函数的解析处理做准备。 */  
    s = path_init(nd, flags);//4.2 nd的path和inode赋值，为path->dentry赋值. 在解析路径的过程中，需要遍历路径中的每个部分，而其中使用结构体struct nameidata来保存当前遍历的状态。path_init()函数主要用来将该结构体初始化
    //path_init返回之后，nd中的字段path就已经设定为起始路径了
    if (IS_ERR(s)) { /* if 判断为 0 */
        ...
    }
    
    while (!(error = link_path_walk(s, nd)) &&//5 关键的字符串解析处理函数，其核心思想是分级解析字符串，通过字符串对应的目录项找到下一级目录的inode节点,并在到达最终目标所在目录的时候停下来(最终的目标要由do_last（）处理)
        (error = do_last(nd, file, op, &opened)) > 0) { //6. 创建或者获取文件对应的inode对象，并且初始化file对象，至此一个表示打开文件的内存对象filp诞生
        nd->flags &= ~(LOOKUP_OPEN|LOOKUP_CREATE|LOOKUP_EXCL);
		s = trailing_symlink(nd);
		if (IS_ERR(s)) {
			error = PTR_ERR(s);
			break;
		}
    }
    terminate_walk(nd);
out2:
    if (!(opened & FILE_OPENED)) { /* if 判断为 0 */
        ...
    }
    if (unlikely(error)) { /* if 判断为 0 */
        ...
    }
    return file;
}

//4.1
/* Find an unused file structure and return a pointer to it.
 * Returns an error pointer if some error happend e.g. we over file
 * structures limit, run out of memory or operation is not permitted.
 *
 * Be very careful using this.  You are responsible for
 * getting write access to any mount that you might assign
 * to this filp, if it is opened for write.  If this is not
 * done, you will imbalance int the mount's writer count
 * and a warning at __fput() time.
 */
struct file *get_empty_filp(void)
{
	const struct cred *cred = current_cred();
	static long old_max;
	struct file *f;
	int error;

	/*
	 * Privileged users can go above max_files
	 */
	if (get_nr_files() >= files_stat.max_files && !capable(CAP_SYS_ADMIN)) {
		/*
		 * percpu_counters are inaccurate.  Do an expensive check before
		 * we go and fail.
		 */
		if (percpu_counter_sum_positive(&nr_files) >= files_stat.max_files)
			goto over;
	}

	f = kmem_cache_zalloc(filp_cachep, GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	percpu_counter_inc(&nr_files);
	f->f_cred = get_cred(cred);
	error = security_file_alloc(f);
	if (unlikely(error)) {
		file_free(f);
		return ERR_PTR(error);
	}

	atomic_long_set(&f->f_count, 1);
	rwlock_init(&f->f_owner.lock);
	spin_lock_init(&f->f_lock);
	mutex_init(&f->f_pos_lock);
	eventpoll_init_file(f);
	/* f->f_version: 0 */
	return f;

over:
	/* Ran out of filps - report that */
	if (get_nr_files() > old_max) {
		pr_info("VFS: file-max limit %lu reached\n", get_max_files());
		old_max = get_nr_files();
	}
	return ERR_PTR(-ENFILE);
}

//4.2path_init对真正遍历路径环境的初始化，主要就是设置变量 nd。这个 nd 是 do_filp_open 里定义的局部变量，是一个临时性的数据结构，用来存储遍历路径的中间结果。
//在解析路径的过程中，需要遍历路径中的每个部分，而其中使用结构体struct nameidata来保存当前遍历的状态。path_init()函数主要用来将该结构体初始化
static const char *path_init(struct nameidata *nd, unsigned flags)
{
    /* 函数入口参数： flags = LOOKUP_FOLLOW | LOOKUP_RCU */
    const char *s = nd->name->name; /* s = "/home/gaobsh/a.txt" */
 
    if (!*s) /* if 判断为 0 */
        ...
 
    nd->last_type = LAST_ROOT; //将 last_type 设置成 LAST_ROOT，意思就是在路径名中只有“/”
    nd->flags = flags | LOOKUP_JUMPED | LOOKUP_PARENT;
    nd->depth = 0;
    if (flags & LOOKUP_ROOT) { /* if 判断为 0 *///LOOKUP_ROOT 可以提供一个路径作为根路径，主要用于两个系统调用 open_by_handle_at 和 sysctl
        ...
    }
 
    nd->root.mnt = NULL;
    nd->path.mnt = NULL;
    nd->path.dentry = NULL;
 
    nd->m_seq = read_seqbegin(&mount_lock); 
    //根据路径名设置起始位置
    if (*s == '/') { /* if 判断为 1，表示从绝对路径‘/’开始解析目录。如果路径是绝对路径（以“/”开头）的话，就把起始路径指向进程的根目录
        if (flags & LOOKUP_RCU) /* if 判断为 1 */
            rcu_read_lock();
        set_root(nd);//设置struct nameidata nd 的值，使得路径解析从根目录开始（而不是当前工作目录）
        if (likely(!nd_jump_root(nd))) /* if 判断为 1 */
            return s; /* 函数由此处返回 */
       nd->root.mnt = NULL;
		rcu_read_unlock();
		return ERR_PTR(-ECHILD);
	} else if (nd->dfd == AT_FDCWD) {//如果路径是相对路径，并且 dfd 是一个特殊值（AT_FDCWD），那就说明起始路径需要指向当前工作目录，也就是 pwd
		if (flags & LOOKUP_RCU) {
			struct fs_struct *fs = current->fs;
			unsigned seq;

			rcu_read_lock();

			do {
				seq = read_seqcount_begin(&fs->seq);
				nd->path = fs->pwd;
				nd->inode = nd->path.dentry->d_inode;
				nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
			} while (read_seqcount_retry(&fs->seq, seq));
		} else {
			get_fs_pwd(current->fs, &nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
		return s;
	} else {//如果给了一个有效的 dfd，那就需要吧起始路径指向这个给定的目录
		/* Caller must check execute permissions on the starting path component */
		struct fd f = fdget_raw(nd->dfd);
		struct dentry *dentry;

		if (!f.file)
			return ERR_PTR(-EBADF);

		dentry = f.file->f_path.dentry;

		if (*s) {
			if (!d_can_lookup(dentry)) {
				fdput(f);
				return ERR_PTR(-ENOTDIR);
			}
		}

		nd->path = f.file->f_path;
		if (flags & LOOKUP_RCU) {
			rcu_read_lock();
			nd->inode = nd->path.dentry->d_inode;
			nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
		} else {
			path_get(&nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
		fdput(f);
		return s;
	}
}

//5.link_path_walk将路径名逐步解析，并最终找到目标文件的struct dentry 结构体（保存在参数 struct nameidata *nd 中）。
//函数返回一个int值可以判断查找操作是否出错，参数name就是从用户空间复制来的path，nd表示查找过程中用来存储临时数据的nameidata结构体。 
//https://www.sohu.com/a/109477788_467784
//https://blog.51cto.com/alanwu/1120652
//link_path_walk函数完成了基本的名字解析功能，是名字字符串解析处理实现的核心。该函数的实现基于分级解析处理的思想。
//例如，当需要解析“/dev/mapper/map0”字符串时，其首先需要判断从何处开始解析？根目录还是当前目录？案例是从根目录开始解析，
//那么获取根目录的dentry对象并开始分析后继字符串。以’/’字符为界按序提取字符串，首先我们可以提取”dev”字符串，并且计算该字符串的hash值，
//通过该hash值查找detry下的inode hash表，就可以得到/dev/目录的inode对象。依次类推，最后解析得到”/dev/mapper/”目录的inode对象以及文件名”map0”。
//至此，link_path_walk函数的使命完成
static int link_path_walk(const char *name, struct nameidata *nd)
{
    int err;
     /* 移除’/’字符,因为linux下，多个连续的“/"等同于1个"/"*/  
    while (*name=='/') 
        name++;
    if (!*name)    // 如果此时name为空表示path就是根目录，那么就不需要pathwalk已经找到了对应的dentry和其他的信息  
        return 0;  
 
    /* At this point we know we have a real path component. */
    for(;;) {
        u64 hash_len;
        int type;
     /* inode访问的permission检查 */  
        err = may_lookup(nd); 
        if (err) /* if 判断为 0 */
            break;
        //计算name（系统调用传入的path），填充quick string结构体
        hash_len = hash_name(nd->path.dentry, name);//5.1 例如/home/ok/1.txt，在本for循环的第一次执行时，hash_name先拿到home
        
        //初始化name（系统调用传入的path）的第一个文件的文件类型，（分为普通文件，.文件和..文件）
        type = LAST_NORM;
         /* LAST_DOT和LAST_DOTDOT情形判断 */  
        if (name[0] == '.') /* if 判断为 0 */
          switch (hashlen_len(hash_len)) {
			case 2:
				if (name[1] == '.') {/* LAST_DOTDOT是上级目录 */  
					type = LAST_DOTDOT;
					nd->flags |= LOOKUP_JUMPED;
				}
				break;
			case 1:/* LAST_DOT是当前目录 */  
				type = LAST_DOT;
		    }
        //一个普通的路径名
        if (likely(type == LAST_NORM)) { /* if 判断为 1 */
             /* LAST_NORM标记说明是需要通过本地目录进行字符串解析 */  
            struct dentry *parent = nd->path.dentry;//dentry 结构
            nd->flags &= ~LOOKUP_JUMPED;
            if (unlikely(parent->d_flags & DCACHE_OP_HASH)) { /* if 判断为 0 *//* 如果该标记有效，需要重新计算hash值 */  
                 err = parent->d_op->d_hash(parent, nd->inode,  
                               &this);  //这里，episode并没有指定d_hash方法，所以使用系统默认的方法
                if (err < 0)  
                    break;  
            }
        }
        //现在可以先把子路径名更新一下，把nd中当前的name设置为上面处理过的name，接着往下走
        nd->last.hash_len = hash_len;
        nd->last.name = name;
        nd->last_type = type;
    
        name += hashlen_len(hash_len);
        //如果此时已经到达了最终目标，那么“路径行走”的任务就完成了
        if (!*name) /* 除了路径中的最后一部分（'a.txt'）if判断为1，其他 if 判断为 0 */
            goto OK;
        /*
         * 如果路径还没到头，那么现在就一定是一个“/”，再次略过连续的“/”,并让 name 指向下一个子路径，为下一次循环做好了准备。
         */
        do {
            name++;
        } while (unlikely(*name == '/'));
        if (unlikely(!*name)) { /* if 判断为 0 */
OK:
            /* pathname body, done */
            if (!nd->depth)
                return 0; /* 解析到路径最后一部分时，该函数由此处返回 */
            ...
        } else {
            /* not the last component */
            //现在，我们可以肯定当前的子路径一定是一个中间节点（文件夹或符号链接），既然是中间节点，那么就需要“走过”这个节点
            err = walk_component(nd, WALK_FOLLOW | WALK_MORE); //5.2 /* err == 0 *///在解析真实的文件名之前，解析路径都是使用此函数，这个会不断覆盖，nd保存的最终内容是文件的直接父目录的信息，祖父目录的信息被冲掉了
        }
        if (err < 0) /* if 判断为 0 */
            ...
 
        if (err) { /* if 判断为 0 */
           ...
        }
        if (unlikely(!d_can_lookup(nd->path.dentry))) { /* if 判断为 0 */  /* 字符串还没有解析完毕，但是当前的inode已经继续不允许解析处理了，所以，返回错误码 */  
            ...
        }
    } /* go back to for loop */
}

//5.1 以父目录对应的dentry为盐，计算要查找的文件名的hash值，
*
 * Calculate the length and hash of the path component, and
 * return the "hash_len" as the result.
 */
static inline u64 hash_name(const void *salt, const char *name)
{
	unsigned long a = 0, b, x = 0, y = (unsigned long)salt;
	unsigned long adata, bdata, mask, len;
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;

	len = 0;
	goto inside;

	do {
		HASH_MIX(x, y, a);
		len += sizeof(unsigned long);
inside:
		a = load_unaligned_zeropad(name+len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) | has_zero(b, &bdata, &constants)));

	adata = prep_zero_mask(a, adata, &constants);
	bdata = prep_zero_mask(b, bdata, &constants);
	mask = create_zero_mask(adata | bdata);
	x ^= a & zero_bytemask(mask);

	return hashlen_create(fold_hash(x, y), len + find_zero(mask));
}

//5.2 walk_component
static int walk_component(struct nameidata *nd, int flags)
{
	struct path path;
	struct inode *inode;
	unsigned seq;
	int err;
	/*
	 * "." and ".." are special - ".." especially so because it has
	 * to be able to know about the current root directory and
	 * parent relationships.
	 */
	if (unlikely(nd->last_type != LAST_NORM)) {//如果当前要处理的路径不是普通路径
		err = handle_dots(nd, nd->last_type);//5.2.1
      
		if (!(flags & WALK_MORE) && nd->depth)
			put_link(nd);
		return err;
	}
    //前面处理.和..,处理完后，进入下一个子路径循环，当前为普通的子路径
    //在 Kernel 中任何一个常用操作都会有两套以上的策略，其中一个是高效率的相对而言另一个就是系统开销比较大的。
	err = lookup_fast(nd, &path, &inode, &seq);//5.2.2
    //首先 Kernel 会在 rcu-walk 模式下进入 lookup_fast 进行尝试，如果失败了那么就尝试就地转入 ref-walk，如果还是不行就回到 do_filp_open 从头开始。
    //Kernel 在 ref-walk 模式下会首先在内存缓冲区查找相应的目标（lookup_fast），如果找不到就启动具体文件系统自己的 lookup 进行查找（lookup_slow）。
    //注意，在 rcu-walk 模式下是不会进入 lookup_slow 的。如果这样都还找不到的话就一定是是出错了，那就报错返回吧，这时屏幕就会出现喜闻乐见的“No such file or directory”。
	if (unlikely(err <= 0)) {//如果fast方式失败，则使用slow方式查询
		if (err < 0)
			return err;
		path.dentry = lookup_slow(&nd->last, nd->path.dentry,
					  nd->flags);
		if (IS_ERR(path.dentry))
			return PTR_ERR(path.dentry);

		path.mnt = nd->path.mnt;
		err = follow_managed(&path, nd);
		if (unlikely(err < 0))
			return err;

		if (unlikely(d_is_negative(path.dentry))) {
			path_to_nameidata(&path, nd);
			return -ENOENT;
		}

		seq = 0;	/* we are already out of RCU mode */
		inode = d_backing_inode(path.dentry);
	}

	return step_into(nd, &path, flags, inode, seq);
}
//5.2.1 
    static inline int handle_dots(struct nameidata *nd, int type)
    {
        //如果是“.”的话那就就代表的是当前路径，直接返回就好了
        if (type == LAST_DOTDOT) {//..”出现在路径里就表示要向“上”走一层，也就是要走到父目录里面去，而父目录一定是存在内存中而且对于当前的进程来说一定也是合法的，否则在读取父目录的时候就已经出错了
            if (nd->flags & LOOKUP_RCU) {
                if (follow_dotdot_rcu(nd))//5.2.1.1
                    return -ECHILD;
            } else
                follow_dotdot(nd);
        }
        return 0;
    }
//5.2.1.1follow_dotdot_rcu,处理..子路径
static int follow_dotdot_rcu(struct nameidata *nd)
{
	struct inode *inode = nd->inode;

	while (1) {
		if (path_equal(&nd->path, &nd->root))
			break;
		if (nd->path.dentry != nd->path.mnt->mnt_root) {
			struct dentry *old = nd->path.dentry;
			struct dentry *parent = old->d_parent;
			unsigned seq;

			inode = parent->d_inode;
			seq = read_seqcount_begin(&parent->d_seq);
			if (unlikely(read_seqcount_retry(&old->d_seq, nd->seq)))
				return -ECHILD;
			nd->path.dentry = parent;
			nd->seq = seq;
			if (unlikely(!path_connected(&nd->path)))
				return -ENOENT;
			break;
		} else {
			struct mount *mnt = real_mount(nd->path.mnt);
			struct mount *mparent = mnt->mnt_parent;
			struct dentry *mountpoint = mnt->mnt_mountpoint;
			struct inode *inode2 = mountpoint->d_inode;
			unsigned seq = read_seqcount_begin(&mountpoint->d_seq);
			if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
				return -ECHILD;
			if (&mparent->mnt == nd->path.mnt)
				break;
			/* we know that mountpoint was pinned */
			nd->path.dentry = mountpoint;
			nd->path.mnt = &mparent->mnt;
			inode = inode2;
			nd->seq = seq;
		}
	}
	while (unlikely(d_mountpoint(nd->path.dentry))) {
		struct mount *mounted;
		mounted = __lookup_mnt(nd->path.mnt, nd->path.dentry);
		if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
			return -ECHILD;
		if (!mounted)
			break;
		nd->path.mnt = &mounted->mnt;
		nd->path.dentry = mounted->mnt.mnt_root;
		inode = nd->path.dentry->d_inode;
		nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
	}
	nd->inode = inode;
	return 0;
}

//5.2.2 http://blog.chinaunix.net/uid-20522771-id-4419703.html
static int lookup_fast(struct nameidata *nd,
		       struct path *path, struct inode **inode,
		       unsigned *seqp)
{
	struct vfsmount *mnt = nd->path.mnt;
	struct dentry *dentry, *parent = nd->path.dentry;
	int status = 1;
	int err;

	/*
	 * Rename seqlock is not required here because in the off chance
	 * of a false negative due to a concurrent rename, the caller is
	 * going to fall back to non-racy lookup.
	 */
	if (nd->flags & LOOKUP_RCU) {//RCU模式
		unsigned seq;
		bool negative;
		dentry = __d_lookup_rcu(parent, &nd->last, &seq);//在内存中的某个散列表里通过字符串比较查找目标 dentry，如果找到了就返回该 dentry；
		if (unlikely(!dentry)) {
			if (unlazy_walk(nd))//如果没找到就使用 unlazy_walk 就地将查找模式切换到 ref-walk
				return -ECHILD;//如果还不行就只好返回到 do_filp_open 从头来过
			return 0;
		}

		/*
		 * This sequence count validates that the inode matches
		 * the dentry name information from lookup.
		 */
		*inode = d_backing_inode(dentry);
		negative = d_is_negative(dentry);
		if (unlikely(read_seqcount_retry(&dentry->d_seq, seq)))//进行检查，确保我们在读取的时候，确保没人修改这些结构
			return -ECHILD;

		/*
		 * This sequence count validates that the parent had no
		 * changes while we did the lookup of the dentry above.
		 *
		 * The memory barrier in read_seqcount_begin of child is
		 *  enough, we can use __read_seqcount_retry here.
		 */
		if (unlikely(__read_seqcount_retry(&parent->d_seq, nd->seq)))//再次检查
			return -ECHILD;

		*seqp = seq;
		status = d_revalidate(dentry, nd->flags);
		if (likely(status > 0)) {
			/*
			 * Note: do negative dentry check after revalidation in
			 * case that drops it.
			 */
			if (unlikely(negative))
				return -ENOENT;
			path->mnt = mnt;//更新path
			path->dentry = dentry;
			if (likely(__follow_mount_rcu(nd, path, inode, seqp)))//nd只记录真正的目录，不记录挂载点和符号链接，所以先跳过这些伪目标
				return 1;
		}
		if (unlazy_child(nd, dentry, seq))
			return -ECHILD;
		if (unlikely(status == -ECHILD))
			/* we'd been told to redo it in non-rcu mode */
			status = d_revalidate(dentry, nd->flags);
	} else {
		dentry = __d_lookup(parent, &nd->last);
		if (unlikely(!dentry))
			return 0;
		status = d_revalidate(dentry, nd->flags);
	}
	if (unlikely(status <= 0)) {
		if (!status)
			d_invalidate(dentry);
		dput(dentry);
		return status;
	}
	if (unlikely(d_is_negative(dentry))) {
		dput(dentry);
		return -ENOENT;
	}

	path->mnt = mnt;
	path->dentry = dentry;
	err = follow_managed(path, nd);
	if (likely(err > 0))
		*inode = d_backing_inode(path->dentry);
	return err;
}

//6. do_last(), nd中放的是文件的父目录对应的信息
static int do_last(struct nameidata *nd,
           struct file *file, const struct open_flags *op,
           int *opened)
{
    struct dentry *dir = nd->path.dentry;//父目录的dentry结构体指针
    int open_flag = op->open_flag;
    bool will_truncate = (open_flag & O_TRUNC) != 0;
    bool got_write = false;
    int acc_mode = op->acc_mode;//访问模式
    unsigned seq;
    struct inode *inode;
    struct path path;
    int error;
 
    nd->flags &= ~LOOKUP_PARENT;
    nd->flags |= op->intent;
 
    if (nd->last_type != LAST_NORM) { /* if 判断为 0 */
        ...
    }
 
    if (!(open_flag & O_CREAT)) { //open函数中是否带了如果文件不存在，则创建之的参数
        if (nd->last.name[nd->last.len]) /* if 判断为 0 */
            ...
        /* we _can_ be in RCU mode here */
        error = lookup_fast(nd, &path, &inode, &seq);
        if (likely(error > 0)) /* if 判断为 1 */
            goto finish_lookup; /* 此处进行跳转 */
        ...
    } else {
       /* create side of things */
		/*
		 * This will *only* deal with leaving RCU mode - LOOKUP_JUMPED
		 * has been cleared when we got to the last component we are
		 * about to look up
		 */
		error = complete_walk(nd);
		if (error)
			return error;

		audit_inode(nd->name, dir, LOOKUP_PARENT);
		/* trailing slashes? */
		if (unlikely(nd->last.name[nd->last.len]))
			return -EISDIR;
    }
   if (open_flag & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR)) {
		error = mnt_want_write(nd->path.mnt);
		if (!error)
			got_write = true;
		/*
		 * do _not_ fail yet - we might not need that or fail with
		 * a different error; let lookup_open() decide; we'll be
		 * dropping this one anyway.
		 */
	}
	if (open_flag & O_CREAT)//需要创建新文件
		inode_lock(dir->d_inode);
	else
		inode_lock_shared(dir->d_inode);
	error = lookup_open(nd, &path, file, op, got_write, opened);//6.1 

	if (error <= 0) {
		if (error)
			goto out;

		if ((*opened & FILE_CREATED) ||
		    !S_ISREG(file_inode(file)->i_mode))
			will_truncate = false;

		audit_inode(nd->name, file->f_path.dentry, 0);
		goto opened;
	}

	if (*opened & FILE_CREATED) {
		/* Don't check for write permission, don't truncate */
		open_flag &= ~O_TRUNC;
		will_truncate = false;
		acc_mode = 0;
		path_to_nameidata(&path, nd);
		goto finish_open_created;
	}

	/*
	 * If atomic_open() acquired write access it is dropped now due to
	 * possible mount and symlink following (this might be optimized away if
	 * necessary...)
	 */
	if (got_write) {
		mnt_drop_write(nd->path.mnt);
		got_write = false;
	}

	error = follow_managed(&path, nd);
	if (unlikely(error < 0))
		return error;

	if (unlikely(d_is_negative(path.dentry))) {
		path_to_nameidata(&path, nd);
		return -ENOENT;
	}

	/*
	 * create/update audit record if it already exists.
	 */
	audit_inode(nd->name, path.dentry, 0);

	if (unlikely((open_flag & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))) {
		path_to_nameidata(&path, nd);
		return -EEXIST;
	}

	seq = 0;	/* out of RCU mode, so the value doesn't matter */
	inode = d_backing_inode(path.dentry);
finish_lookup:
    error = step_into(nd, &path, 0, inode, seq);
    if (unlikely(error)) /* if 判断为 0 */
        ...
finish_open:
    /* Why this, you ask?  _Now_ we might have grown LOOKUP_JUMPED... */
    error = complete_walk(nd);
    if (error) /* if 判断为 0 */
        ...
    audit_inode(nd->name, nd->path.dentry, 0);
    error = -EISDIR;
    if ((open_flag & O_CREAT) && d_is_dir(nd->path.dentry)) /* if 判断为 0 */
        ...
    error = -ENOTDIR;
    if ((nd->flags & LOOKUP_DIRECTORY) && !d_can_lookup(nd->path.dentry)) /* if 判断为 0 */
        ...
    if (!d_is_reg(nd->path.dentry)) /* if 判断为 0 */
        ...
 
    if (will_truncate) { /* if 判断为 0 */
        ...
    }
finish_open_created:
    error = may_open(&nd->path, acc_mode, open_flag);//主要用来检查相应打开权限
    if (error) /* if 判断为 0 */
        ...
    BUG_ON(*opened & FILE_OPENED); /* once it's opened, it's opened */
    error = vfs_open(&nd->path, file, current_cred()); //6. 真正执行文件打开的操作 ,真正的“打开”操作是在这里进行的，前面所有的操作都是为了“找到”这个文件
    if (error)/* if 判断为 0 */  
        ...   
    *opened |= FILE_OPENED;  
opened:  
    error = open_check_o_direct(file); /* 这里 error == 0 */  
    if (!error) /* if 判断为 1 */  
        error = ima_file_check(file, op->acc_mode, *opened); /* 这里 error == 0 */  
    if (!error && will_truncate) /* if 判断为 0 */  
        ...  
out:  
    if (unlikely(error) && (*opened & FILE_OPENED))/* if 判断为 0 */  
        ...  
    if (unlikely(error > 0)) {/* if 判断为 0 */  
        ... 
    }  
    if (got_write)/* if 判断为 0 */  
        ...  
    return error;  
}

//6.1
/*
 * Look up and maybe create and open the last component.
 *
 * Must be called with i_mutex held on parent.
 *
 * Returns 0 if the file was successfully atomically created (if necessary) and
 * opened.  In this case the file will be returned attached to @file.
 *
 * Returns 1 if the file was not completely opened at this time, though lookups
 * and creations will have been performed and the dentry returned in @path will
 * be positive upon return if O_CREAT was specified.  If O_CREAT wasn't
 * specified then a negative dentry may be returned.
 *
 * An error code is returned otherwise.
 *
 * FILE_CREATE will be set in @*opened if the dentry was created and will be
 * cleared otherwise prior to returning.
 */
static int lookup_open(struct nameidata *nd, struct path *path,
			struct file *file,
			const struct open_flags *op,
			bool got_write, int *opened)
{
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	int open_flag = op->open_flag;
	struct dentry *dentry;
	int error, create_error = 0;
	umode_t mode = op->mode;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	if (unlikely(IS_DEADDIR(dir_inode)))
		return -ENOENT;

	*opened &= ~FILE_CREATED;
	dentry = d_lookup(dir, &nd->last);
	for (;;) {
		if (!dentry) {
			dentry = d_alloc_parallel(dir, &nd->last, &wq);
			if (IS_ERR(dentry))
				return PTR_ERR(dentry);
		}
		if (d_in_lookup(dentry))
			break;

		error = d_revalidate(dentry, nd->flags);
		if (likely(error > 0))
			break;
		if (error)
			goto out_dput;
		d_invalidate(dentry);
		dput(dentry);
		dentry = NULL;
	}
	if (dentry->d_inode) {
		/* Cached positive dentry: will open in f_op->open */
		goto out_no_open;
	}

	/*
	 * Checking write permission is tricky, bacuse we don't know if we are
	 * going to actually need it: O_CREAT opens should work as long as the
	 * file exists.  But checking existence breaks atomicity.  The trick is
	 * to check access and if not granted clear O_CREAT from the flags.
	 *
	 * Another problem is returing the "right" error value (e.g. for an
	 * O_EXCL open we want to return EEXIST not EROFS).
	 */
	if (open_flag & O_CREAT) {
		if (!IS_POSIXACL(dir->d_inode))
			mode &= ~current_umask();
		if (unlikely(!got_write)) {
			create_error = -EROFS;
			open_flag &= ~O_CREAT;
			if (open_flag & (O_EXCL | O_TRUNC))
				goto no_open;
			/* No side effects, safe to clear O_CREAT */
		} else {
			create_error = may_o_create(&nd->path, dentry, mode);
			if (create_error) {
				open_flag &= ~O_CREAT;
				if (open_flag & O_EXCL)
					goto no_open;
			}
		}
	} else if ((open_flag & (O_TRUNC|O_WRONLY|O_RDWR)) &&
		   unlikely(!got_write)) {
		/*
		 * No O_CREATE -> atomicity not a requirement -> fall
		 * back to lookup + open
		 */
		goto no_open;
	}

	if (dir_inode->i_op->atomic_open) {
		error = atomic_open(nd, dentry, path, file, op, open_flag,
				    mode, opened);
		if (unlikely(error == -ENOENT) && create_error)
			error = create_error;
		return error;
	}

no_open:
	if (d_in_lookup(dentry)) {
		struct dentry *res = dir_inode->i_op->lookup(dir_inode, dentry,
							     nd->flags);//6.1.1 这里调用了episode_dir_inode_operations中的lookup()
		d_lookup_done(dentry);
		if (unlikely(res)) {
			if (IS_ERR(res)) {
				error = PTR_ERR(res);
				goto out_dput;
			}
			dput(dentry);
			dentry = res;
		}
	}

	/* Negative dentry, just create the file */
	if (!dentry->d_inode && (open_flag & O_CREAT)) {
		*opened |= FILE_CREATED;
		audit_inode_child(dir_inode, dentry, AUDIT_TYPE_CHILD_CREATE);
		if (!dir_inode->i_op->create) {//6.1.2 这里调用了episode_dir_inode_operations中的create()
			error = -EACCES;
			goto out_dput;
		}
		error = dir_inode->i_op->create(dir_inode, dentry, mode,
						open_flag & O_EXCL);//6.1.2episode_dir_inode_operations中的create()
		if (error)
			goto out_dput;
		fsnotify_create(dir_inode, dentry);
	}
	if (unlikely(create_error) && !dentry->d_inode) {
		error = create_error;
		goto out_dput;
	}
out_no_open:
	path->dentry = dentry;
	path->mnt = nd->path.mnt;
	return 1;

out_dput:
	dput(dentry);
	return error;
}
//6.1.1
static struct dentry *episode_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode = NULL;
	ino_t ino;

	if (dentry->d_name.len > episode_sb(dir->i_sb)->s_namelen)//6.1.1.1
		return ERR_PTR(-ENAMETOOLONG);

	ino = episode_inode_by_name(dentry);//6.1.1.2
	if (ino) {
		inode = episode_iget(dir->i_sb, ino);//6.1.1.3
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry, inode);
	return NULL;
}
//6.1.1.1
static inline struct episode_sb_info *episode_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}
//6.1.1.2
ino_t episode_inode_by_name(struct dentry *dentry)
{
	struct page *page;
	struct episode_dir_entry *de = episode_find_entry(dentry, &page);//6.1.1.2.1
	ino_t res = 0;

	if (de) {
		struct address_space *mapping = page->mapping;
		struct inode *inode = mapping->host;
		struct episode_sb_info *sbi = episode_sb(inode->i_sb);//6.1.1.1

		if (sbi->s_version == EPISODE_V)
			res = de->inode;
		dir_put_page(page);
	}
	return res;
}
//6.1.1.2.1
/*
 *	episode_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
episode_dirent *episode_find_entry(struct dentry *dentry, struct page **res_page)
{
	const char * name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct inode * dir = d_inode(dentry->d_parent);
	struct super_block * sb = dir->i_sb;
	struct episode_sb_info * sbi = episode_sb(sb);
	unsigned long n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	char *p;

	char *namx;
	__u32 inumber;
	*res_page = NULL;

	for (n = 0; n < npages; n++) {
		char *kaddr, *limit;

		page = dir_get_page(dir, n);
		if (IS_ERR(page))
			continue;

		kaddr = (char*)page_address(page);
		limit = kaddr + episode_last_byte(dir, n) - sbi->s_dirsize;//6.1.1.2.1.1
		for (p = kaddr; p <= limit; p = episode_next_entry(p, sbi)) {//6.1.1.2.1.2
			if (sbi->s_version == EPISODE_V) {
				episode_dirent *de = (episode_dirent *)p;
				namx = de->name;
				inumber = de->inode;
				if (!inumber)
					continue;
				if (namecompare(namelen, sbi->s_namelen, name, namx))
					goto found;
 			}
			
		}
		dir_put_page(page);
	}
	return NULL;

found:
	*res_page = page;
	return (episode_dirent *)p;
}
//6.1.1.2.1.1
/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned
episode_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = PAGE_SIZE;

	if (page_nr == (inode->i_size >> PAGE_SHIFT))
		last_byte = inode->i_size & (PAGE_SIZE - 1);
	return last_byte;
}
//6.1.1.2.1.2
static inline void *episode_next_entry(void *de, struct episode_sb_info *sbi)
{
	return (void*)((char*)de + sbi->s_dirsize);
}

//6.1.1.3

/*
 * The global function to read an inode.
 */
struct inode *episode_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	if (INODE_VERSION(inode) == EPISODE_V)
		return __episode_iget(inode);//6.1.1.3.1
	else
		return NULL;
}
//6.1.1.3.1
/*
 * The episode function to read an inode.
 */
static struct inode *__episode_iget(struct inode *inode)
{
	struct buffer_head * bh;
	struct episode_inode * raw_inode;
	struct episode_inode_info *episode_inode = episode_i(inode);//6.1.1.3.1.1
	int i;

	raw_inode = episode_raw_inode(inode->i_sb, inode->i_ino, &bh);//6.1.1.3.1.2
	if (!raw_inode) {
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}
	inode->i_mode = raw_inode->i_mode;
	i_uid_write(inode, raw_inode->i_uid);
	i_gid_write(inode, raw_inode->i_gid);
	set_nlink(inode, raw_inode->i_nlinks);
	inode->i_size = raw_inode->i_size;
	inode->i_mtime.tv_sec = raw_inode->i_mtime;
	inode->i_atime.tv_sec = raw_inode->i_atime;
	inode->i_ctime.tv_sec = raw_inode->i_ctime;
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = 0;
	for (i = 0; i < 10; i++)
		episode_inode->u.i2_data[i] = raw_inode->i_zone[i];
	episode_set_inode(inode, old_decode_dev(raw_inode->i_zone[0]));//6.1.1.3.1.3
	brelse(bh);
	unlock_new_inode(inode);
	return inode;
}
//6.1.1.3.1.1
static inline struct episode_inode_info *episode_i(struct inode *inode)
{
	return container_of(inode, struct episode_inode_info, vfs_inode);
}
//6.1.1.3.1.2
struct episode_inode * episode_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
	int block;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct episode_inode *p;
	int episode_inodes_per_block = sb->s_blocksize / sizeof(struct episode_inode);

	*bh = NULL;
	if (!ino || ino > sbi->s_ninodes) {
		printk("Bad inode number on dev %s: %ld is out of range\n",
		       sb->s_id, (long)ino);
		return NULL;
	}
	ino--;
	block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
		 ino / episode_inodes_per_block;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read inode block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
	return p + ino % episode_inodes_per_block;
}

//6.1.1.3.1.3
void episode_set_inode(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &episode_file_inode_operations;
		inode->i_fop = &episode_file_operations;
		inode->i_mapping->a_ops = &episode_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &episode_dir_inode_operations;
		inode->i_fop = &episode_dir_operations;
		inode->i_mapping->a_ops = &episode_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &episode_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &episode_aops;
	} else
		init_special_inode(inode, inode->i_mode, rdev);
}

//6.1.2
static int episode_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return episode_mknod(dir, dentry, mode, 0);//6.1.2.1
}
//6.1.2.1
static int episode_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	int error;
	struct inode *inode;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	inode = episode_new_inode(dir, mode, &error);//6.1.2.1.1

	if (inode) {
		episode_set_inode(inode, rdev);//6.1.2.1.2
		mark_inode_dirty(inode);
		error = add_nondir(dentry, inode);
	}
	return error;
}
//6.1.2.1.1
struct inode *episode_new_inode(const struct inode *dir, umode_t mode, int *error)
{
	struct super_block *sb = dir->i_sb;
	struct episode_sb_info *sbi = episode_sb(sb);
	struct inode *inode = new_inode(sb);
	struct buffer_head * bh;
	int bits_per_zone = 8 * sb->s_blocksize;
	unsigned long j;
	int i;

	if (!inode) {
		*error = -ENOMEM;
		return NULL;
	}
	j = bits_per_zone;
	bh = NULL;
	*error = -ENOSPC;
	spin_lock(&bitmap_lock);
	for (i = 0; i < sbi->s_imap_blocks; i++) {
		bh = sbi->s_imap[i];
		j = episode_find_first_zero_bit(bh->b_data, bits_per_zone);//6.1.2.1.1.1
		if (j < bits_per_zone)
			break;
	}
	if (!bh || j >= bits_per_zone) {
		spin_unlock(&bitmap_lock);
		iput(inode);
		return NULL;
	}
	if (episode_test_and_set_bit(j, bh->b_data)) {	/* shouldn't happen *///6.1.2.1.1.2
		spin_unlock(&bitmap_lock);
		printk("episode_new_inode: bit already set\n");
		iput(inode);
		return NULL;
	}
	spin_unlock(&bitmap_lock);
	mark_buffer_dirty(bh);
	j += i * bits_per_zone;
	if (!j || j > sbi->s_ninodes) {
		iput(inode);
		return NULL;
	}
	inode_init_owner(inode, dir, mode);
	inode->i_ino = j;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;
	memset(&episode_i(inode)->u, 0, sizeof(episode_i(inode)->u));
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	*error = 0;
	return inode;
}
//6.1.2.1.1.1
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
//6.1.2.1.1.2
#define episode_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr) ^ 16, (unsigned long *)(addr))
//6.1.2.1.2
void episode_set_inode(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &episode_file_inode_operations;
		inode->i_fop = &episode_file_operations;
		inode->i_mapping->a_ops = &episode_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &episode_dir_inode_operations;
		inode->i_fop = &episode_dir_operations;
		inode->i_mapping->a_ops = &episode_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &episode_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &episode_aops;
	} else
		init_special_inode(inode, inode->i_mode, rdev);
}

//7.
/**
 * vfs_open - open the file at the given path
 * @path: path to open
 * @file: newly allocated file with f_flag initialized
 * @cred: credentials to use
 */
int vfs_open(const struct path *path, struct file *file,
	     const struct cred *cred)
{
	struct dentry *dentry = d_real(path->dentry, NULL, file->f_flags, 0);

	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	file->f_path = *path;
	return do_dentry_open(file, d_backing_inode(dentry), NULL, cred);//8.
}
//8.do_dentry_open()完成文件打开的操作，在该函数中文件对象 struct file *file 的大部分成员变量被填充
static int do_dentry_open(struct file *f,
			  struct inode *inode,
			  int (*open)(struct inode *, struct file *),
			  const struct cred *cred)
{
	static const struct file_operations empty_fops = {};
	int error;

	f->f_mode = OPEN_FMODE(f->f_flags) | FMODE_LSEEK |
				FMODE_PREAD | FMODE_PWRITE;

	path_get(&f->f_path);
	f->f_inode = inode;
	f->f_mapping = inode->i_mapping;

	/* Ensure that we skip any errors that predate opening of the file */
	f->f_wb_err = filemap_sample_wb_err(f->f_mapping);

	if (unlikely(f->f_flags & O_PATH)) {
		f->f_mode = FMODE_PATH;
		f->f_op = &empty_fops;
		return 0;
	}

	/* Any file opened for execve()/uselib() has to be a regular file. */
	if (unlikely(f->f_flags & FMODE_EXEC && !S_ISREG(inode->i_mode))) {
		error = -EACCES;
		goto cleanup_file;
	}

	if (f->f_mode & FMODE_WRITE && !special_file(inode->i_mode)) {//写模式，且不是特殊文件
		error = get_write_access(inode);
		if (unlikely(error))
			goto cleanup_file;
		error = __mnt_want_write(f->f_path.mnt);
		if (unlikely(error)) {
			put_write_access(inode);
			goto cleanup_file;
		}
		f->f_mode |= FMODE_WRITER;
	}

	/* POSIX.1-2008/SUSv4 Section XSI 2.9.7 */
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))
		f->f_mode |= FMODE_ATOMIC_POS;

	f->f_op = fops_get(inode->i_fop);
	if (unlikely(WARN_ON(!f->f_op))) {
		error = -ENODEV;
		goto cleanup_all;
	}

	error = security_file_open(f, cred);
	if (error)
		goto cleanup_all;

	error = break_lease(locks_inode(f), f->f_flags);
	if (error)
		goto cleanup_all;

	if (!open)
		open = f->f_op->open;//9. 真正执行open的函数，是在文件系统的file的f_op中指定的
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	if ((f->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(inode);
	if ((f->f_mode & FMODE_READ) &&
	     likely(f->f_op->read || f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) &&
	     likely(f->f_op->write || f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;

	f->f_write_hint = WRITE_LIFE_NOT_SET;
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	file_ra_state_init(&f->f_ra, f->f_mapping->host->i_mapping);

	return 0;

cleanup_all:
	fops_put(f->f_op);
	if (f->f_mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(f->f_path.mnt);
	}
cleanup_file:
	path_put(&f->f_path);
	f->f_path.mnt = NULL;
	f->f_path.dentry = NULL;
	f->f_inode = NULL;
	return error;
}
//9. 由于episode_file_operations没有提供对open()的实例化，所以这里调用的是系统默认的open函数。这一部分涉及到driver相关内容，就不看了

//由于灭有找到默认的open方法，所以open到此为止。下面的generic_file_open也不是默认的open方法
/*
 * Called when an inode is about to be open.
 * We use this to disallow opening large files on 32bit systems if
 * the caller didn't specify O_LARGEFILE.  On 64bit systems we force
 * on this flag in sys_open.
 */
int generic_file_open(struct inode * inode, struct file * filp)
{
	if (!(filp->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EOVERFLOW;
	return 0;
}