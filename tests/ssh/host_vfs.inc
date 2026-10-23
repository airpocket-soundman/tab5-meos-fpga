/* Test-only Win32-backed MeOS VFS. Logical /sd and /usb map below a temporary
 * test directory, while /outside exists to detect export-containment bugs. */
static char host_vfs_root[MAX_PATH];
static struct { HANDLE file; uint32_t flags; } host_fds[VFS_FD_MAX];
static unsigned host_io_limit, host_open_count;
int vfs_normalize(const char *path,char *out) {
    if(!path || !out || path[0]!='/')return ERR_INVAL;
    size_t used=1,at=1;out[0]='/';out[1]=0;
    while(path[at]) {
        while(path[at]=='/')at++;
        size_t start=at;
        while(path[at] && path[at]!='/')at++;
        size_t n=at-start;
        if(!n || (n==1 && path[start]=='.'))continue;
        if(n==2 && path[start]=='.' && path[start+1]=='.') {
            while(used>1 && out[used-1]!='/')used--;
            if(used>1)used--;
            out[used]=0;continue;
        }
        if(used+(used>1)+n>=VFS_PATH_MAX)return ERR_NAMETOOLONG;
        if(used>1)out[used++]='/';
        memcpy(out+used,path+start,n);used+=n;out[used]=0;
    }
    return 0;
}
static int host_path(const char *logical,char physical[MAX_PATH]) {
    char normal[VFS_PATH_MAX];
    int e=vfs_normalize(logical,normal);if(e)return e;
    if(strchr(normal,'\\') || strchr(normal,':'))return ERR_INVAL;
    size_t a=strlen(host_vfs_root),b=strlen(normal);
    if(a+b>=MAX_PATH)return ERR_NAMETOOLONG;
    memcpy(physical,host_vfs_root,a);memcpy(physical+a,normal,b+1);
    return 0;
}
static int host_error(void) {
    DWORD e=GetLastError();
    if(e==ERROR_FILE_NOT_FOUND || e==ERROR_PATH_NOT_FOUND)return ERR_NOENT;
    if(e==ERROR_ACCESS_DENIED)return ERR_PERM;
    if(e==ERROR_ALREADY_EXISTS || e==ERROR_FILE_EXISTS)return ERR_EXIST;
    if(e==ERROR_DIR_NOT_EMPTY)return ERR_NOTEMPTY;
    return ERR_IO;
}
static bool host_fd_valid(int fd) {
    return fd>=0 && fd<VFS_FD_MAX && host_fds[fd].file && host_fds[fd].file!=INVALID_HANDLE_VALUE;
}
int vfs_open(const char *path,uint32_t flags) {
    char name[MAX_PATH];int e=host_path(path,name);if(e)return e;
    if(!(flags&(VFS_O_READ|VFS_O_WRITE)))return ERR_INVAL;
    int fd=0;while(fd<VFS_FD_MAX && host_fd_valid(fd))fd++;
    if(fd==VFS_FD_MAX)return ERR_BUSY;
    DWORD access=((flags&VFS_O_READ)?GENERIC_READ:0)|((flags&VFS_O_WRITE)?GENERIC_WRITE:0);
    DWORD create=(flags&VFS_O_EXCL)?CREATE_NEW:
                 (flags&VFS_O_CREATE)?((flags&VFS_O_TRUNC)?CREATE_ALWAYS:OPEN_ALWAYS):
                 (flags&VFS_O_TRUNC)?TRUNCATE_EXISTING:OPEN_EXISTING;
    /* Exercise a file appearing after SFTP STAT but before atomic OPEN. */
    if((flags&VFS_O_EXCL) && strstr(path,"/race.bin")) {
        HANDLE rival=CreateFileA(name,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(rival!=INVALID_HANDLE_VALUE) {
            DWORD n;WriteFile(rival,"RACE_ORIGINAL",13,&n,NULL);CloseHandle(rival);
        }
    }
    HANDLE h=CreateFileA(name,access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                         NULL,create,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)return host_error();
    host_fds[fd].file=h;host_fds[fd].flags=flags;host_open_count++;
    return fd;
}
int vfs_read(int fd,void *buffer,size_t length) {
    if(!host_fd_valid(fd))return ERR_BADF;
    if(!(host_fds[fd].flags&VFS_O_READ))return ERR_PERM;
    if(host_io_limit && length>host_io_limit)length=host_io_limit;
    DWORD n=0;return ReadFile(host_fds[fd].file,buffer,(DWORD)length,&n,NULL)?(int)n:host_error();
}
int vfs_write(int fd,const void *buffer,size_t length) {
    if(!host_fd_valid(fd))return ERR_BADF;
    if(!(host_fds[fd].flags&VFS_O_WRITE))return ERR_PERM;
    if(host_fds[fd].flags&VFS_O_APPEND) {
        LARGE_INTEGER zero={0};if(!SetFilePointerEx(host_fds[fd].file,zero,NULL,FILE_END))return host_error();
    }
    if(host_io_limit && length>host_io_limit)length=host_io_limit;
    DWORD n=0;return WriteFile(host_fds[fd].file,buffer,(DWORD)length,&n,NULL)?(int)n:host_error();
}
int vfs_seek(int fd,int32_t offset,int whence) {
    if(!host_fd_valid(fd))return ERR_BADF;
    LARGE_INTEGER off={.QuadPart=offset},where;
    if(whence<0 || whence>2)return ERR_INVAL;
    if(!SetFilePointerEx(host_fds[fd].file,off,&where,(DWORD)whence))return host_error();
    return where.QuadPart>INT32_MAX ? ERR_INVAL : (int)where.QuadPart;
}
int vfs_close(int fd) {
    if(!host_fd_valid(fd))return ERR_BADF;
    bool ok=CloseHandle(host_fds[fd].file)!=0;
    host_fds[fd].file=NULL;host_open_count--;
    return ok?0:ERR_IO;
}
int vfs_stat(const char *path,vfs_info_t *out) {
    char name[MAX_PATH];int e=host_path(path,name);if(e)return e;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if(!GetFileAttributesExA(name,GetFileExInfoStandard,&attr))return host_error();
    if(attr.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)return ERR_PERM;
    if(attr.nFileSizeHigh)return ERR_INVAL;
    memset(out,0,sizeof(*out));
    out->type=(attr.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?FS_TYPE_DIR:FS_TYPE_FILE;
    out->size=attr.nFileSizeLow;
    const char *base=strrchr(path,'/');base=base?base+1:path;
    if(strlen(base)>FS_NAME_MAX)return ERR_NAMETOOLONG;
    strcpy(out->name,base);return 0;
}
int vfs_readdir(const char *path,int (*cb)(const vfs_info_t *,void *),void *ctx) {
    char name[MAX_PATH];int e=host_path(path,name);if(e)return e;
    size_t n=strlen(name);if(n+3>=MAX_PATH)return ERR_NAMETOOLONG;
    memcpy(name+n,"/*",3);
    WIN32_FIND_DATAA entry;HANDLE find=FindFirstFileA(name,&entry);
    if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?0:host_error();
    int count=0;
    do {
        if(!strcmp(entry.cFileName,".") || !strcmp(entry.cFileName,".."))continue;
        if(strlen(entry.cFileName)>FS_NAME_MAX || entry.nFileSizeHigh ||
           (entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        vfs_info_t info={0};strcpy(info.name,entry.cFileName);
        info.type=(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?FS_TYPE_DIR:FS_TYPE_FILE;
        info.size=entry.nFileSizeLow;count++;
        if(cb(&info,ctx))break;
    } while(FindNextFileA(find,&entry));
    FindClose(find);return count;
}
int vfs_mkdir(const char *path) {
    char name[MAX_PATH];int e=host_path(path,name);if(e)return e;
    return CreateDirectoryA(name,NULL)?0:host_error();
}
int vfs_unlink(const char *path) {
    char name[MAX_PATH];int e=host_path(path,name);if(e)return e;
    DWORD attrs=GetFileAttributesA(name);if(attrs==INVALID_FILE_ATTRIBUTES)return host_error();
    BOOL ok=(attrs&FILE_ATTRIBUTE_DIRECTORY)?RemoveDirectoryA(name):DeleteFileA(name);
    return ok?0:host_error();
}
