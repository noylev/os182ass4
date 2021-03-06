//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  uint off;
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, &off)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;
  char sym_path[FILENAMESIZE];

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  }
  else {
    int result = read_link_to_buf(path, sym_path, FILENAMESIZE);
    if (DEBUG > 1) cprintf("open: got %s as sym_path from path %s, result %d\n", sym_path, path, result);
    if (result > -1) {
      // Is symbolic link, is it valid?
      if ((ip = namei(sym_path)) == 0) {
        if (DEBUG > 0) cprintf("open: failed resolving sym_path %s", sym_path);
        // Error.
        end_op();
        cprintf("open: fail\n");
        return -1;
      }
    }
    else {
      // Load the inode.
      if ((ip = namei(path)) == 0) {
        if (DEBUG > 0) cprintf("open: failed opening path %s", path);
        // Not found!
          end_op();
          return -1;
      }
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  char sym_path[FILENAMESIZE];
  struct inode *ip;

  if(argstr(0, &path) < 0)
    return -1;

  // Check if this is a symbolic link.
  int result = read_link_to_buf(path, sym_path, FILENAMESIZE);
  if (DEBUG > 1) cprintf("CHDIR: got %s as sym_path from path %s, result %d\n", sym_path, path, result);
  if (result > -1) {
    // Is symbolic link, is it valid?
    if ((ip = namei(sym_path)) == 0) {
      if (DEBUG > 1) cprintf("CHDIR: failed resolving sym_path %s", sym_path);
      // Error.
      return -1;
    }
  }
  else {
    // Load the inode.
    if ((ip = namei(path)) == 0) {
      // Not found!
      return -1;
    }
  }

  ilock(ip);
  if(ip->type != T_DIR){
    // Not a directory.
    if (DEBUG > 1) cprintf("CHDIR: ip->type not T_DIR, is %d\n", ip->type);
    iunlockput(ip);
    return -1;
  }
  iunlock(ip);
  iput(myproc()->cwd);
  myproc()->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

// Task 2
int
sys_symlink(void)
{
  char *target, *path;
  struct file *f;
  struct inode *ip;


  if (argstr(0, &target) < 0 || argstr(1, &path) < 0)
    return -1;

  begin_op();
  ip = create(path, T_SYMLINK, 0, 0);
  end_op();

  if(ip == 0)
    return -1;

  if ((f = filealloc()) == 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    return -1;
  }

  // Change the inode.
  if (strlen(target) > FILENAMESIZE)
    panic("Symbolic link path is too long ");

  safestrcpy((char*)ip->addrs, target, FILENAMESIZE);
  ip->size = 0;
  iunlock(ip);

  f->type = FD_SYMLNK;
  f->ip = ip;
  f->off = 0;
  f->readable = 1;
  f->writable = 0;

  return 0;
}

int
sys_readlink(void)
{
  char *path;
  char *buf;
  uint bufsiz;

  if(argstr(0, &path) < 0 || argstr(1, &buf) < 0  || argint(2, (int*)&bufsiz) < 0)
    return -1;

  return read_link_to_buf(path, buf, bufsiz);
}


// Saves the target name in buf. (Not syscall)
int
read_link_to_buf(char* path, char* buf, uint bufsiz)
{
  struct inode *ip, *sym_ip;
  int i;

  if (DEBUG > 1) cprintf("SYMLINK: checking path %s\n", path);

  if (strlen(path) > FILENAMESIZE) {
    // Path too long.
    return -1;
  }

  if((ip = namei(path)) == 0) {
    if (DEBUG > 1) cprintf("SYMLINK: namei is empty for %s\n", path);
    return -1;
  }
  ilock(ip);

  if (!(ip->type == T_SYMLINK)) {
    if (DEBUG > 1) cprintf("SYMLINK: %s not a symlink.\n", path);
    iunlock(ip);
    return -1;
  }

  for (i = 0; i < MAX_DEREFERENCE ; i++) {
    if((sym_ip = namei((char*)ip->addrs)) == 0) {
      if (DEBUG > 1) cprintf("SYMLINK: could not load address %s.\n", (char*) ip->addrs);
      iunlock(ip);
      return -1;
    }
      if (DEBUG > 1) cprintf("SYMLINK: loaded address %s.\n", (char*) ip->addrs);

    if (sym_ip->type == T_SYMLINK) {
      iunlock(ip);
      ip = sym_ip;
      ilock(ip);
    }
    else {
      break;
    }
  }

  if (i == MAX_DEREFERENCE) {
    panic("symbolic link exceeds MAX_DEREFERENCE ");
  }
  ilock(sym_ip);
  if (DEBUG > 1) cprintf("SYMLINK: final sym_ip->type = %d.\n", sym_ip->type);

  if (sym_ip->type == T_FILE || sym_ip->type == T_DIR) {
    safestrcpy(buf, (char*)ip->addrs, bufsiz);
    iunlock(ip);
    iunlock(sym_ip);
    if (DEBUG > 1) cprintf("SYMLINK: final result: %s.\n", buf);
    return strlen(buf);
  }
  iunlock(ip);
  iunlock(sym_ip);
  return -1;
}


// Task 3
int
sys_ftag(void) {
    int fd, ret;
    char *key;
    char *val;
    struct file *file_ptr;

    if(argfd(0, &fd, &file_ptr) < 0 || argstr(1, &key) < 0  || argstr(2, &val) < 0)
        return -1;

    begin_op();
    ret = fs_ftag(file_ptr, key, val);
    end_op();
    return ret;
}

int sys_funtag(void) {
    int fd, ret;
    char *key;
    struct file *file_ptr;

    if(argfd(0, &fd, &file_ptr) < 0 || argstr(1, &key) < 0)
        return -1;

    begin_op();
    ret = fs_funtag(file_ptr, key);
    end_op();
    return ret;
}

int sys_gettag(void) {
    int fd,ret;
    char *key;
    char *buf;
    struct file *file_ptr;

    if(argfd(0, &fd, &file_ptr) < 0 || argstr(1, &key) < 0 || argstr(2, &buf) < 0)
        return -1;

    begin_op();
    ret = fs_gettag(file_ptr,key,buf);
    end_op();
    return ret;
}
