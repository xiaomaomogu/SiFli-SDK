/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffsfs.h"
#include "yaffs_guts.h"
#include "yaffscfg.h"
#include "yportenv.h"
#include "yaffs_trace.h"

#include "string.h"

#define YAFFS_MAX_RW_SIZE   0x70000000
#define YAFFSFS_MAX_SYMLINK_DEREFERENCES 5

#ifndef NULL
    #define NULL ((void *)0)
#endif

#define ROOT_DIR(dev) (((dev) && (dev)->is_mounted) ? (dev)->root_dir : NULL)

/* YAFFSFS_RW_SIZE must be a power of 2 */
#define YAFFSFS_RW_SHIFT (13)
#define YAFFSFS_RW_SIZE  (1<<YAFFSFS_RW_SHIFT)

/* Some forward references */
static struct yaffs_obj *yaffsfs_FindObject(struct yaffs_obj *relativeDirectory,
        const YCHAR *path,
        int symDepth, int getEquiv,
        struct yaffs_obj **dirOut,
        int *notDir, int *loop);

static void yaffsfs_RemoveObjectCallback(struct yaffs_obj *obj);
static yaffs_DIR *yaffsfs_opendir_reldir_no_lock(
    struct yaffs_obj *reldir, const YCHAR *dirname);
static int yaffsfs_closedir_no_lock(yaffs_DIR *dirent);

unsigned int yaffs_wr_attempts;

/*
 * Handle management.
 * There are open inodes in struct yaffsfs_Inode.
 * There are open file descriptors in yaffsfs_FileDes.
 * There are open handles in yaffsfs_FileDes.
 *
 * Things are structured this way to be like the Linux VFS model
 * so that interactions with the yaffs guts calls are similar.
 * That means more common code paths and less special code.
 * That means better testing etc.
 *
 * We have 3 layers because:
 * A handle is different than an fd because you can use dup()
 * to create a new handle that accesses the *same* fd. The two
 * handles will use the same offset (part of the fd). We only close
 * down the fd when there are no more handles accessing it.
 *
 * More than one fd can currently access one file, but each fd
 * has its own permsiions and offset.
 */

struct yaffsfs_Inode
{
    int count;      /* Number of handles accessing this inode */
    struct yaffs_obj *iObj;
};


struct yaffsfs_DirSearchContext
{
    struct yaffs_dirent de; /* directory entry */
    YCHAR name[NAME_MAX + 1];   /* name of directory being searched */
    struct yaffs_obj *dirObj;   /* ptr to directory being searched */
    struct yaffs_obj *nextReturn;   /* obj  returned by next readddir */
    struct list_head others;
    s32 offset: 20;
    u8 inUse: 1;
};

struct yaffsfs_FileDes
{
    u8 isDir: 1;        /* This s a directory */
    u8 reading: 1;
    u8 writing: 1;
    u8 append: 1;
    u8 shareRead: 1;
    u8 shareWrite: 1;
    s32 inodeId: 12;    /* Index to corresponding yaffsfs_Inode */
    s32 handleCount: 10; /* Number of handles for this fd */
    union
    {
        Y_LOFF_T position;  /* current position in file */
        yaffs_DIR *dir;
    } v;
};

struct yaffsfs_Handle
{
    short int fdId;
    short int useCount;
};


static struct yaffsfs_DirSearchContext yaffsfs_dsc[YAFFSFS_N_DSC];
static struct yaffsfs_Inode yaffsfs_inode[YAFFSFS_N_HANDLES];
static struct yaffsfs_FileDes yaffsfs_fd[YAFFSFS_N_HANDLES];
static struct yaffsfs_Handle yaffsfs_handle[YAFFSFS_N_HANDLES];

static int yaffsfs_handlesInitialised;

unsigned yaffs_set_trace(unsigned tm)
{
    yaffs_trace_mask = tm;
    return yaffs_trace_mask;
}

unsigned yaffs_get_trace(void)
{
    return yaffs_trace_mask;
}

/*
 * yaffsfs_InitHandle
 * Inilitalise handle management on start-up.
 */

static void yaffsfs_InitHandles(void)
{
    int i;

    if (yaffsfs_handlesInitialised)
        return;

    yaffsfs_handlesInitialised = 1;

    memset(yaffsfs_inode, 0, sizeof(yaffsfs_inode));
    memset(yaffsfs_fd, 0, sizeof(yaffsfs_fd));
    memset(yaffsfs_handle, 0, sizeof(yaffsfs_handle));
    memset(yaffsfs_dsc, 0, sizeof(yaffsfs_dsc));

    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
        yaffsfs_fd[i].inodeId = -1;
    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
        yaffsfs_handle[i].fdId = -1;
}

static struct yaffsfs_Handle *yaffsfs_HandleToPointer(int h)
{
    if (h >= 0 && h < YAFFSFS_N_HANDLES)
        return &yaffsfs_handle[h];
    return NULL;
}

static struct yaffsfs_FileDes *yaffsfs_HandleToFileDes(int handle)
{
    struct yaffsfs_Handle *h = yaffsfs_HandleToPointer(handle);

    if (h && h->useCount > 0 && h->fdId >= 0 && h->fdId < YAFFSFS_N_HANDLES)
        return &yaffsfs_fd[h->fdId];

    return NULL;
}

static struct yaffsfs_Inode *yaffsfs_HandleToInode(int handle)
{
    struct yaffsfs_FileDes *fd = yaffsfs_HandleToFileDes(handle);

    if (fd && fd->handleCount > 0 &&
            fd->inodeId >= 0 && fd->inodeId < YAFFSFS_N_HANDLES)
        return &yaffsfs_inode[fd->inodeId];

    return NULL;
}

static struct yaffs_obj *yaffsfs_HandleToObject(int handle)
{
    struct yaffsfs_Inode *in = yaffsfs_HandleToInode(handle);

    if (in)
        return in->iObj;

    return NULL;
}

/*
 * yaffsfs_FindInodeIdForObject
 * Find the inode entry for an object, if it exists.
 */

static int yaffsfs_FindInodeIdForObject(struct yaffs_obj *obj)
{
    int i;
    int ret = -1;

    if (obj)
        obj = yaffs_get_equivalent_obj(obj);

    /* Look for it in open inode table */
    for (i = 0; i < YAFFSFS_N_HANDLES && ret < 0; i++)
    {
        if (yaffsfs_inode[i].iObj == obj)
            ret = i;
    }
    return ret;
}

/*
 * yaffsfs_GetInodeIdForObject
 * Grab an inode entry when opening a new inode.
 */
static int yaffsfs_GetInodeIdForObject(struct yaffs_obj *obj)
{
    int i;
    int ret;
    struct yaffsfs_Inode *in = NULL;

    if (obj)
        obj = yaffs_get_equivalent_obj(obj);

    ret = yaffsfs_FindInodeIdForObject(obj);

    for (i = 0; i < YAFFSFS_N_HANDLES && ret < 0; i++)
    {
        if (!yaffsfs_inode[i].iObj)
            ret = i;
    }

    if (ret >= 0)
    {
        in = &yaffsfs_inode[ret];
        if (!in->iObj)
            in->count = 0;
        in->iObj = obj;
        in->count++;
    }

    return ret;
}

static int yaffsfs_CountHandles(struct yaffs_obj *obj)
{
    int i = yaffsfs_FindInodeIdForObject(obj);

    if (i >= 0)
        return yaffsfs_inode[i].count;
    else
        return 0;
}

static void yaffsfs_ReleaseInode(struct yaffsfs_Inode *in)
{
    struct yaffs_obj *obj;

    obj = in->iObj;
    obj->my_inode = NULL;
    in->iObj = NULL;

    if (obj->unlinked)
        yaffs_del_obj(obj);
}

static void yaffsfs_PutInode(int inodeId)
{
    if (inodeId >= 0 && inodeId < YAFFSFS_N_HANDLES)
    {
        struct yaffsfs_Inode *in = &yaffsfs_inode[inodeId];
        in->count--;
        if (in->count <= 0)
        {
            yaffsfs_ReleaseInode(in);
            in->count = 0;
        }
    }
}

static int yaffsfs_NewHandle(struct yaffsfs_Handle **hptr)
{
    int i;
    struct yaffsfs_Handle *h;

    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
    {
        h = &yaffsfs_handle[i];
        if (h->useCount < 1)
        {
            memset(h, 0, sizeof(struct yaffsfs_Handle));
            h->fdId = -1;
            h->useCount = 1;
            if (hptr)
                *hptr = h;
            return i;
        }
    }
    return -1;
}

static int yaffsfs_NewHandleAndFileDes(void)
{
    int i;
    struct yaffsfs_FileDes *fd;
    struct yaffsfs_Handle *h = NULL;
    int handle = yaffsfs_NewHandle(&h);

    if (handle < 0)
        return -1;

    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
    {
        fd = &yaffsfs_fd[i];
        if (fd->handleCount < 1)
        {
            memset(fd, 0, sizeof(struct yaffsfs_FileDes));
            fd->inodeId = -1;
            fd->handleCount = 1;
            h->fdId = i;
            return handle;
        }
    }

    /* Dump the handle because we could not get a fd */
    h->useCount = 0;
    return -1;
}

/*
 * yaffs_get_handle
 * Increase use of handle when reading/writing a file
 * Also gets the file descriptor.
 */

static int yaffsfs_GetHandle(int handle)
{
    struct yaffsfs_Handle *h = yaffsfs_HandleToPointer(handle);

    if (h && h->useCount > 0)
    {
        h->useCount++;
        return 0;
    }
    return -1;
}

/*
 * yaffs_put_handle
 * Let go of a handle when closing a file or aborting an open or
 * ending a read or write.
 */

static int yaffsfs_PutFileDes(int fdId)
{
    struct yaffsfs_FileDes *fd;

    if (fdId >= 0 && fdId < YAFFSFS_N_HANDLES)
    {
        fd = &yaffsfs_fd[fdId];
        fd->handleCount--;
        if (fd->handleCount < 1)
        {
            if (fd->isDir)
            {
                yaffsfs_closedir_no_lock(fd->v.dir);
                fd->v.dir = NULL;
            }
            if (fd->inodeId >= 0)
            {
                yaffsfs_PutInode(fd->inodeId);
                fd->inodeId = -1;
            }
        }
    }
    return 0;
}

static int yaffsfs_PutHandle(int handle)
{
    struct yaffsfs_Handle *h = yaffsfs_HandleToPointer(handle);

    if (h && h->useCount > 0)
    {
        h->useCount--;
        if (h->useCount < 1)
        {
            yaffsfs_PutFileDes(h->fdId);
            h->fdId = -1;
        }
    }

    return 0;
}

static void yaffsfs_BreakDeviceHandles(struct yaffs_dev *dev)
{
    struct yaffsfs_FileDes *fd;
    struct yaffsfs_Handle *h;
    struct yaffs_obj *obj;
    int i;
    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
    {
        h = yaffsfs_HandleToPointer(i);
        fd = yaffsfs_HandleToFileDes(i);
        obj = yaffsfs_HandleToObject(i);
        if (h && h->useCount > 0)
        {
            h->useCount = 0;
            h->fdId = 0;
        }
        if (fd && fd->handleCount > 0 && obj && obj->my_dev == dev)
        {
            fd->handleCount = 0;
            yaffsfs_PutInode(fd->inodeId);
            fd->inodeId = -1;
        }
    }
}

/*
 *  Stuff to handle names.
 */
#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
#ifndef CONFIG_YAFFS_WINCE
static int yaffs_toupper(YCHAR a)
{
    if (a >= 'a' && a <= 'z')
        return (a - 'a') + 'A';
    else
        return a;
}
#endif

static int yaffsfs_Match(YCHAR a, YCHAR b)
{
    return (yaffs_toupper(a) == yaffs_toupper(b));
}
#else
static int yaffsfs_Match(YCHAR a, YCHAR b)
{
    /* case sensitive */
    return (a == b);
}
#endif

static int yaffsfs_IsPathDivider(YCHAR ch)
{
    const YCHAR *str = YAFFS_PATH_DIVIDERS;

    while (*str)
    {
        if (*str == ch)
            return 1;
        str++;
    }

    return 0;
}

static int yaffsfs_CheckNameLength(const YCHAR *name)
{
    int retVal = 0;

    int nameLength = yaffs_strnlen(name, YAFFS_MAX_NAME_LENGTH + 1);

    if (nameLength == 0)
    {
        yaffsfs_SetError(-ENOENT);
        retVal = -1;
    }
    else if (nameLength > YAFFS_MAX_NAME_LENGTH)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        retVal = -1;
    }

    return retVal;
}

static int yaffsfs_alt_dir_path(const YCHAR *path, YCHAR **ret_path)
{
    YCHAR *alt_path = NULL;
    int path_length;
    int i;

    /*
     * We don't have a definition for max path length.
     * We will use 3 * max name length instead.
     */
    *ret_path = NULL;
    path_length = yaffs_strnlen(path, (YAFFS_MAX_NAME_LENGTH + 1) * 3 + 1);

    /* If the last character is a path divider, then we need to
     * trim it back so that the name look-up works properly.
     * eg. /foo/new_dir/ -> /foo/newdir
     * Curveball: Need to handle multiple path dividers:
     * eg. /foof/sdfse///// -> /foo/sdfse
     */
    if (path_length > 0 && yaffsfs_IsPathDivider(path[path_length - 1]))
    {
        alt_path = kmalloc(path_length + 1, 0);
        if (!alt_path)
            return -1;
        yaffs_strcpy(alt_path, path);
        for (i = path_length - 1;
                i >= 0 && yaffsfs_IsPathDivider(alt_path[i]); i--)
            alt_path[i] = (YCHAR) 0;
    }
    *ret_path = alt_path;
    return 0;
}

LIST_HEAD(yaffsfs_deviceList);

/*
 * yaffsfs_FindDevice
 * yaffsfs_FindRoot
 * Scan the configuration list to find the device
 * Curveballs: Should match paths that end in '/' too
 * Curveball2 Might have "/x/ and "/x/y". Need to return the longest match
 */
static struct yaffs_dev *yaffsfs_FindDevice(const YCHAR *path,
        YCHAR **restOfPath)
{
    struct list_head *cfg;
    const YCHAR *leftOver;
    const YCHAR *p;
    struct yaffs_dev *retval = NULL;
    struct yaffs_dev *dev = NULL;
    int thisMatchLength;
    int longestMatch = -1;
    int matching;

    /*
     * Check all configs, choose the one that:
     * 1) Actually matches a prefix (ie /a amd /abc will not match
     * 2) Matches the longest.
     */
    list_for_each(cfg, &yaffsfs_deviceList)
    {
        dev = list_entry(cfg, struct yaffs_dev, dev_list);
        leftOver = path;
        p = dev->param.name;
        thisMatchLength = 0;
        matching = 1;


        if (!p)
            continue;

        /* Skip over any leading  /s */
        while (yaffsfs_IsPathDivider(*p))
            p++;
        while (yaffsfs_IsPathDivider(*leftOver))
            leftOver++;

        while (matching && *p && *leftOver)
        {

            /* Now match the text part */
            while (matching &&
                    *p && !yaffsfs_IsPathDivider(*p) &&
                    *leftOver && !yaffsfs_IsPathDivider(*leftOver))
            {
                if (yaffsfs_Match(*p, *leftOver))
                {
                    p++;
                    leftOver++;
                    thisMatchLength++;
                }
                else
                {
                    matching = 0;
                }
            }

            if ((*p && !yaffsfs_IsPathDivider(*p)) ||
                    (*leftOver && !yaffsfs_IsPathDivider(*leftOver)))
                matching = 0;
            else
            {
                while (yaffsfs_IsPathDivider(*p))
                    p++;
                while (yaffsfs_IsPathDivider(*leftOver))
                    leftOver++;
            }
        }

        /* Skip over any /s in leftOver */
        while (yaffsfs_IsPathDivider(*leftOver))
            leftOver++;

        /*Skip over any /s in p */
        while (yaffsfs_IsPathDivider(*p))
            p++;

        /* p should now be at the end of the string if fully matched */
        if (*p)
            matching = 0;

        if (matching && (thisMatchLength > longestMatch))
        {
            /* Matched prefix */
            *restOfPath = (YCHAR *) leftOver;
            retval = dev;
            longestMatch = thisMatchLength;
        }
    }
    return retval;
}

static int yaffsfs_CheckPath(const YCHAR *path)
{
    int n = 0;
    int divs = 0;

    while (*path && n < YAFFS_MAX_NAME_LENGTH && divs < 100)
    {
        if (yaffsfs_IsPathDivider(*path))
        {
            n = 0;
            divs++;
        }
        else
            n++;
        path++;
    }

    return (*path) ? -1 : 0;
}

/* FindMountPoint only returns a dev entry if the path is a mount point */
static struct yaffs_dev *yaffsfs_FindMountPoint(const YCHAR *path)
{
    struct yaffs_dev *dev;
    YCHAR *restOfPath = NULL;

    dev = yaffsfs_FindDevice(path, &restOfPath);
    if (dev && restOfPath && *restOfPath)
        dev = NULL;
    return dev;
}

static struct yaffs_obj *yaffsfs_FindRoot(const YCHAR *path,
        YCHAR **restOfPath)
{
    struct yaffs_dev *dev;

    dev = yaffsfs_FindDevice(path, restOfPath);
    if (dev && dev->is_mounted)
        return dev->root_dir;

    return NULL;
}

static struct yaffs_obj *yaffsfs_FollowLink(struct yaffs_obj *obj,
        int symDepth, int *loop)
{

    if (obj)
        obj = yaffs_get_equivalent_obj(obj);

    while (obj && obj->variant_type == YAFFS_OBJECT_TYPE_SYMLINK)
    {
        YCHAR *alias = obj->variant.symlink_variant.alias;

        if (yaffsfs_IsPathDivider(*alias))
            /*
             * Starts with a /, need to scan from root up */
            /* NB Might not work if this is called with root != NULL
            */
            obj = yaffsfs_FindObject(NULL, alias, symDepth++,
                                     1, NULL, NULL, loop);
        else
            /*
             * Relative to here so use the parent of the
             * symlink as a start
             */
            obj = yaffsfs_FindObject(obj->parent, alias, symDepth++,
                                     1, NULL, NULL, loop);
    }
    return obj;
}

/*
 * yaffsfs_FindDirectory
 * Parse a path to determine the directory and the name within the directory.
 *
 * eg. "/data/xx/ff" --> puts name="ff" and returns the directory "/data/xx"
 */
static struct yaffs_obj *yaffsfs_DoFindDirectory(struct yaffs_obj *startDir,
        const YCHAR *path,
        YCHAR **name, int symDepth,
        int *notDir, int *loop)
{
    struct yaffs_obj *dir;
    YCHAR *restOfPath;
    YCHAR str[YAFFS_MAX_NAME_LENGTH + 1];
    int i;

    if (symDepth > YAFFSFS_MAX_SYMLINK_DEREFERENCES)
    {
        if (loop)
            *loop = 1;
        return NULL;
    }

    if (startDir)
    {
        dir = startDir;
        restOfPath = (YCHAR *) path;
    }
    else
        dir = yaffsfs_FindRoot(path, &restOfPath);

    while (dir)
    {
        /*
         * parse off /.
         * curve ball: also throw away surplus '/'
         * eg. "/ram/x////ff" gets treated the same as "/ram/x/ff"
         */
        while (yaffsfs_IsPathDivider(*restOfPath))
            restOfPath++;   /* get rid of '/' */

        *name = restOfPath;
        i = 0;

        while (*restOfPath && !yaffsfs_IsPathDivider(*restOfPath))
        {
            if (i < YAFFS_MAX_NAME_LENGTH)
            {
                str[i] = *restOfPath;
                str[i + 1] = '\0';
                i++;
            }
            restOfPath++;
        }

        if (!*restOfPath)
            /* got to the end of the string */
            return dir;
        else
        {
            if (yaffs_strcmp(str, _Y(".")) == 0)
            {
                /* Do nothing */
            }
            else if (yaffs_strcmp(str, _Y("..")) == 0)
            {
                dir = dir->parent;
            }
            else
            {
                dir = yaffs_find_by_name(dir, str);

                dir = yaffsfs_FollowLink(dir, symDepth, loop);

                if (dir && dir->variant_type !=
                        YAFFS_OBJECT_TYPE_DIRECTORY)
                {
                    if (notDir)
                        *notDir = 1;
                    dir = NULL;
                }

            }
        }
    }
    /* directory did not exist. */
    return NULL;
}

static struct yaffs_obj *yaffsfs_FindDirectory(struct yaffs_obj *relDir,
        const YCHAR *path,
        YCHAR **name,
        int symDepth,
        int *notDir, int *loop)
{
    return yaffsfs_DoFindDirectory(relDir, path, name, symDepth, notDir,
                                   loop);
}

/*
 * yaffsfs_FindObject turns a path for an existing object into the object
 */
static struct yaffs_obj *yaffsfs_FindObject(struct yaffs_obj *relDir,
        const YCHAR *path, int symDepth,
        int getEquiv,
        struct yaffs_obj **dirOut,
        int *notDir, int *loop)
{
    struct yaffs_obj *dir;
    struct yaffs_obj *obj;
    YCHAR *name;

    dir =
        yaffsfs_FindDirectory(relDir, path, &name, symDepth, notDir, loop);

    if (dirOut)
        *dirOut = dir;

    /* At this stage we have looked up directory part and have the name part
     * in name if there is one.
     *
     *  eg /nand/x/ will give us a name of ""
     *     /nand/x will give us a name of "x"
     *
     * Since the name part might be "." or ".." which need to be fixed.
     */
    if (dir && (yaffs_strcmp(name, _Y("..")) == 0))
    {
        dir = dir->parent;
        obj = dir;
    }
    else if (dir && (yaffs_strcmp(name, _Y(".")) == 0))
        obj = dir;
    else if (dir && *name)
        obj = yaffs_find_by_name(dir, name);
    else
        obj = dir;

    if (getEquiv)
        obj = yaffs_get_equivalent_obj(obj);

    return obj;
}

/*************************************************************************
 *  Start of yaffsfs visible functions.
 *************************************************************************/

int yaffs_dup(int handle)
{
    int newHandleNumber = -1;
    struct yaffsfs_FileDes *existingFD = NULL;
    struct yaffsfs_Handle *existingHandle = NULL;
    struct yaffsfs_Handle *newHandle = NULL;

    yaffsfs_Lock();
    existingHandle = yaffsfs_HandleToPointer(handle);
    existingFD = yaffsfs_HandleToFileDes(handle);
    if (existingFD)
        newHandleNumber = yaffsfs_NewHandle(&newHandle);
    if (newHandle)
    {
        newHandle->fdId = existingHandle->fdId;
        existingFD->handleCount++;
    }

    yaffsfs_Unlock();

    if (!existingFD)
        yaffsfs_SetError(-EBADF);
    else if (!newHandle)
        yaffsfs_SetError(-ENOMEM);

    return newHandleNumber;

}

static int yaffsfs_TooManyObjects(struct yaffs_dev *dev)
{
    int current_objects = dev->n_obj - dev->n_deleted_files;

    if (dev->param.max_objects && current_objects > dev->param.max_objects)
        return 1;
    else
        return 0;
}

int yaffs_open_sharing_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                              int oflag, int mode, int sharing)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    YCHAR *name;
    int handle = -1;
    struct yaffsfs_FileDes *fd = NULL;
    int openDenied = 0;
    int symDepth = 0;
    int errorReported = 0;
    int rwflags = oflag & (O_RDWR | O_RDONLY | O_WRONLY);
    u8 shareRead = (sharing & YAFFS_SHARE_READ) ? 1 : 0;
    u8 shareWrite = (sharing & YAFFS_SHARE_WRITE) ? 1 : 0;
    u8 sharedReadAllowed;
    u8 sharedWriteAllowed;
    u8 alreadyReading;
    u8 alreadyWriting;
    u8 readRequested;
    u8 writeRequested;
    int notDir = 0;
    int loop = 0;
    int is_dir = 0;
    yaffs_DIR *dsc = NULL;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    /* O_EXCL only has meaning if O_CREAT is specified */
    if (!(oflag & O_CREAT))
        oflag &= ~(O_EXCL);

    /* O_TRUNC has no meaning if (O_CREAT | O_EXCL) is specified */
    if ((oflag & O_CREAT) && (oflag & O_EXCL))
        oflag &= ~(O_TRUNC);

    /* Todo: Are there any more flag combos to sanitise ? */

    /* Figure out if reading or writing is requested */

    readRequested = (rwflags == O_RDWR || rwflags == O_RDONLY) ? 1 : 0;
    writeRequested = (rwflags == O_RDWR || rwflags == O_WRONLY) ? 1 : 0;

    yaffsfs_Lock();

    handle = yaffsfs_NewHandleAndFileDes();

    if (handle < 0)
    {
        yaffsfs_SetError(-ENFILE);
        errorReported = __LINE__;
    }
    else
    {

        fd = yaffsfs_HandleToFileDes(handle);

        /* try to find the exisiting object */
        obj = yaffsfs_FindObject(reldir, path, 0, 1, NULL, NULL, NULL);

        obj = yaffsfs_FollowLink(obj, symDepth++, &loop);

        if (obj &&
                obj->variant_type != YAFFS_OBJECT_TYPE_FILE &&
                obj->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
            obj = NULL;

        if (obj)
        {

            /* The file already exists or it might be a directory */
            is_dir = (obj->variant_type ==
                      YAFFS_OBJECT_TYPE_DIRECTORY);

            /*
             * A directory can't be opened except for read, so we
             * ignore other flags
             */
            if (is_dir)
            {
                writeRequested = 0;
                readRequested = 1;
                rwflags = O_RDONLY;
            }

            if (is_dir)
            {
                dsc = yaffsfs_opendir_reldir_no_lock(reldir, path);
                if (!dsc)
                {
                    openDenied = __LINE__;
                    yaffsfs_SetError(-ENFILE);
                    errorReported = __LINE__;
                }
            }

            /* Open should fail if O_CREAT and O_EXCL are specified
             * for a file that exists.
             */
            if (!errorReported &&
                    (oflag & O_EXCL) && (oflag & O_CREAT))
            {
                openDenied = __LINE__;
                yaffsfs_SetError(-EEXIST);
                errorReported = __LINE__;
            }

            /* Check file permissions */
            if (readRequested && !(obj->yst_mode & S_IRUSR))
                openDenied = __LINE__;

            if (writeRequested && !(obj->yst_mode & S_IWUSR))
                openDenied = __LINE__;

            if (!errorReported && writeRequested &&
                    obj->my_dev->read_only)
            {
                openDenied = __LINE__;
                yaffsfs_SetError(-EROFS);
                errorReported = __LINE__;
            }

            if (openDenied && !errorReported)
            {
                yaffsfs_SetError(-EACCES);
                errorReported = __LINE__;
            }

            /* Check sharing of an existing object. */
            if (!openDenied)
            {
                struct yaffsfs_FileDes *fdx;
                int i;

                sharedReadAllowed = 1;
                sharedWriteAllowed = 1;
                alreadyReading = 0;
                alreadyWriting = 0;
                for (i = 0; i < YAFFSFS_N_HANDLES; i++)
                {
                    fdx = &yaffsfs_fd[i];
                    if (fdx->handleCount > 0 &&
                            fdx->inodeId >= 0 &&
                            yaffsfs_inode[fdx->inodeId].iObj
                            == obj)
                    {
                        if (!fdx->shareRead)
                            sharedReadAllowed = 0;
                        if (!fdx->shareWrite)
                            sharedWriteAllowed = 0;
                        if (fdx->reading)
                            alreadyReading = 1;
                        if (fdx->writing)
                            alreadyWriting = 1;
                    }
                }

                if ((!sharedReadAllowed && readRequested) ||
                        (!shareRead && alreadyReading) ||
                        (!sharedWriteAllowed && writeRequested) ||
                        (!shareWrite && alreadyWriting))
                {
                    openDenied = __LINE__;
                    yaffsfs_SetError(-EBUSY);
                    errorReported = __LINE__;
                }
            }

        }

        /* If we could not open an existing object, then let's see if
         * the directory exists. If not, error.
         */
        if (!obj && !errorReported)
        {
            dir = yaffsfs_FindDirectory(reldir, path, &name, 0,
                                        &notDir, &loop);
            if (!dir && notDir)
            {
                yaffsfs_SetError(-ENOTDIR);
                errorReported = __LINE__;
            }
            else if (loop)
            {
                yaffsfs_SetError(-ELOOP);
                errorReported = __LINE__;
            }
            else if (!dir)
            {
                yaffsfs_SetError(-ENOENT);
                errorReported = __LINE__;
            }
        }

        if (!obj && dir && !errorReported && (oflag & O_CREAT))
        {
            /* Let's see if we can create this file */
            if (dir->my_dev->read_only)
            {
                yaffsfs_SetError(-EROFS);
                errorReported = __LINE__;
            }
            else if (yaffsfs_TooManyObjects(dir->my_dev))
            {
                yaffsfs_SetError(-ENFILE);
                errorReported = __LINE__;
            }
            else
                obj = yaffs_create_file(dir, name, mode, 0, 0);

            if (!obj && !errorReported)
            {
                yaffsfs_SetError(-ENOSPC);
                errorReported = __LINE__;
            }
        }

        if (!obj && dir && !errorReported && !(oflag & O_CREAT))
        {
            yaffsfs_SetError(-ENOENT);
            errorReported = __LINE__;
        }

        if (obj && !openDenied)
        {
            int inodeId = yaffsfs_GetInodeIdForObject(obj);

            if (inodeId < 0)
            {
                /*
                 * Todo: Fix any problem if inodes run out,
                 * That can't happen if the number of inode
                 * items >= number of handles.
                 */
            }

            fd->inodeId = inodeId;
            fd->reading = readRequested;
            fd->writing = writeRequested;
            fd->append = (oflag & O_APPEND) ? 1 : 0;
            fd->shareRead = shareRead;
            fd->shareWrite = shareWrite;
            fd->isDir = is_dir;

            if (is_dir)
                fd->v.dir = dsc;
            else
                fd->v.position = 0;

            /* Hook inode to object */
            obj->my_inode = (void *)&yaffsfs_inode[inodeId];

            if (!is_dir && (oflag & O_TRUNC) && fd->writing)
                yaffs_resize_file(obj, 0);
        }
        else
        {
            if (dsc)
                yaffsfs_closedir_no_lock(dsc);
            dsc = NULL;
            yaffsfs_PutHandle(handle);
            if (!errorReported)
                yaffsfs_SetError(0);    /* Problem */
            handle = -1;
        }
    }

    yaffsfs_Unlock();

    return handle;
}

int yaffs_open_sharing_reldev(struct yaffs_dev *dev, const YCHAR *path, int oflag,
                              int mode, int sharing)
{
    return yaffs_open_sharing_reldir(ROOT_DIR(dev), path,
                                     oflag, mode, sharing);
}

int yaffs_open_sharing(const YCHAR *path, int oflag, int mode, int sharing)
{
    return yaffs_open_sharing_reldir(NULL, path, oflag, mode, sharing);
}

int yaffs_open_reldir(struct yaffs_obj *reldir, const YCHAR *path, int oflag, int mode)
{
    return yaffs_open_sharing_reldir(reldir, path, oflag, mode,
                                     YAFFS_SHARE_READ | YAFFS_SHARE_WRITE);
}

int yaffs_open_reldev(struct yaffs_dev *dev, const YCHAR *path, int oflag, int mode)
{
    return yaffs_open_sharing_reldir(ROOT_DIR(dev), path, oflag, mode,
                                     YAFFS_SHARE_READ | YAFFS_SHARE_WRITE);
}

int yaffs_open(const YCHAR *path, int oflag, int mode)
{
    return yaffs_open_reldir(NULL, path, oflag, mode);
}

static int yaffs_Dofsync(int handle, int datasync)
{
    int retVal = -1;
    struct yaffs_obj *obj;

    yaffsfs_Lock();

    obj = yaffsfs_HandleToObject(handle);

    if (!obj)
        yaffsfs_SetError(-EBADF);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else
    {
        yaffs_flush_file(obj, 1, datasync, 0);
        retVal = 0;
    }

    yaffsfs_Unlock();

    return retVal;
}

int yaffs_fsync(int handle)
{
    return yaffs_Dofsync(handle, 0);
}

int yaffs_flush(int handle)
{
    return yaffs_fsync(handle);
}

int yaffs_fdatasync(int handle)
{
    return yaffs_Dofsync(handle, 1);
}

int yaffs_close(int handle)
{
    struct yaffsfs_Handle *h = NULL;
    struct yaffsfs_FileDes *f;
    struct yaffs_obj *obj = NULL;
    int retVal = -1;

    yaffsfs_Lock();

    h = yaffsfs_HandleToPointer(handle);
    f = yaffsfs_HandleToFileDes(handle);
    obj = yaffsfs_HandleToObject(handle);

    if (!h || !obj || !f)
        yaffsfs_SetError(-EBADF);
    else
    {
        /* clean up */
        if (!f->isDir)
            yaffs_flush_file(obj, 1, 0, 1);
        yaffsfs_PutHandle(handle);
        retVal = 0;
    }

    yaffsfs_Unlock();

    return retVal;
}

static int yaffsfs_do_read(int handle, void *vbuf, unsigned int nbyte,
                           int isPread, Y_LOFF_T offset)
{
    struct yaffsfs_FileDes *fd = NULL;
    struct yaffs_obj *obj = NULL;
    Y_LOFF_T pos = 0;
    Y_LOFF_T startPos = 0;
    Y_LOFF_T endPos = 0;
    int nRead = 0;
    int nToRead = 0;
    int totalRead = 0;
    Y_LOFF_T maxRead;
    u8 *buf = (u8 *) vbuf;

    if (yaffsfs_CheckMemRegion(vbuf, nbyte, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    fd = yaffsfs_HandleToFileDes(handle);
    obj = yaffsfs_HandleToObject(handle);

    if (!fd || !obj)
    {
        /* bad handle */
        yaffsfs_SetError(-EBADF);
        totalRead = -1;
    }
    else if (!fd->reading)
    {
        /* Not a reading handle */
        yaffsfs_SetError(-EINVAL);
        totalRead = -1;
    }
    else if (nbyte > YAFFS_MAX_RW_SIZE)
    {
        yaffsfs_SetError(-EINVAL);
        totalRead = -1;
    }
    else
    {
        if (isPread)
            startPos = offset;
        else
            startPos = fd->v.position;

        pos = startPos;

        if (yaffs_get_obj_length(obj) > pos)
            maxRead = yaffs_get_obj_length(obj) - pos;
        else
            maxRead = 0;

        if (nbyte > maxRead)
            nbyte = maxRead;

        yaffsfs_GetHandle(handle);

        endPos = pos + nbyte;

        if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE ||
                nbyte > YAFFS_MAX_RW_SIZE ||
                endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE)
        {
            totalRead = -1;
            nbyte = 0;
        }

        while (nbyte > 0)
        {
            nToRead = YAFFSFS_RW_SIZE -
                      (pos & (YAFFSFS_RW_SIZE - 1));
            if (nToRead > (int)nbyte)
                nToRead = nbyte;

            /* Tricky bit...
             * Need to reverify object in case the device was
             * unmounted in another thread.
             */
            obj = yaffsfs_HandleToObject(handle);
            if (!obj)
                nRead = 0;
            else
                nRead = yaffs_file_rd(obj, buf, pos, nToRead);

            if (nRead > 0)
            {
                totalRead += nRead;
                pos += nRead;
                buf += nRead;
            }

            if (nRead == nToRead)
                nbyte -= nRead;
            else
                nbyte = 0;  /* no more to read */

            if (nbyte > 0)
            {
                yaffsfs_Unlock();
                yaffsfs_Lock();
            }

        }

        yaffsfs_PutHandle(handle);

        if (!isPread)
        {
            if (totalRead >= 0)
                fd->v.position = startPos + totalRead;
            else
                yaffsfs_SetError(-EINVAL);
        }

    }

    yaffsfs_Unlock();

    return (totalRead >= 0) ? totalRead : -1;

}

int yaffs_read(int handle, void *buf, unsigned int nbyte)
{
    return yaffsfs_do_read(handle, buf, nbyte, 0, 0);
}

int yaffs_pread(int handle, void *buf, unsigned int nbyte, Y_LOFF_T offset)
{
    return yaffsfs_do_read(handle, buf, nbyte, 1, offset);
}

static int yaffsfs_do_write(int handle, const void *vbuf, unsigned int nbyte,
                            int isPwrite, Y_LOFF_T offset)
{
    struct yaffsfs_FileDes *fd = NULL;
    struct yaffs_obj *obj = NULL;
    Y_LOFF_T pos = 0;
    Y_LOFF_T startPos = 0;
    Y_LOFF_T endPos;
    int nWritten = 0;
    int totalWritten = 0;
    int write_trhrough = 0;
    int nToWrite = 0;
    const u8 *buf = (const u8 *)vbuf;

    if (yaffsfs_CheckMemRegion(vbuf, nbyte, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    fd = yaffsfs_HandleToFileDes(handle);
    obj = yaffsfs_HandleToObject(handle);

    if (!fd || !obj)
    {
        /* bad handle */
        yaffsfs_SetError(-EBADF);
        totalWritten = -1;
    }
    else if (!fd->writing)
    {
        yaffsfs_SetError(-EINVAL);
        totalWritten = -1;
    }
    else if (obj->my_dev->read_only)
    {
        yaffsfs_SetError(-EROFS);
        totalWritten = -1;
    }
    else
    {
        if (fd->append)
            startPos = yaffs_get_obj_length(obj);
        else if (isPwrite)
            startPos = offset;
        else
            startPos = fd->v.position;

        yaffsfs_GetHandle(handle);
        pos = startPos;
        endPos = pos + nbyte;

        if (pos < 0 || pos > YAFFS_MAX_FILE_SIZE ||
                nbyte > YAFFS_MAX_RW_SIZE ||
                endPos < 0 || endPos > YAFFS_MAX_FILE_SIZE)
        {
            totalWritten = -1;
            nbyte = 0;
        }

        while (nbyte > 0)
        {

            nToWrite = YAFFSFS_RW_SIZE -
                       (pos & (YAFFSFS_RW_SIZE - 1));
            if (nToWrite > (int)nbyte)
                nToWrite = nbyte;

            /* Tricky bit...
             * Need to reverify object in case the device was
             * remounted or unmounted in another thread.
             */
            obj = yaffsfs_HandleToObject(handle);
            if (!obj || obj->my_dev->read_only)
                nWritten = 0;
            else
                nWritten =
                    yaffs_wr_file(obj, buf, pos, nToWrite,
                                  write_trhrough);
            if (nWritten > 0)
            {
                totalWritten += nWritten;
                pos += nWritten;
                buf += nWritten;
            }

            if (nWritten == nToWrite)
                nbyte -= nToWrite;
            else
                nbyte = 0;

            if (nWritten < 1 && totalWritten < 1)
            {
                yaffsfs_SetError(-ENOSPC);
                totalWritten = -1;
            }

            if (nbyte > 0)
            {
                yaffsfs_Unlock();
                yaffsfs_Lock();
            }
        }

        yaffsfs_PutHandle(handle);

        if (!isPwrite)
        {
            if (totalWritten > 0)
                fd->v.position = startPos + totalWritten;
            else
                yaffsfs_SetError(-EINVAL);
        }
    }

    yaffsfs_Unlock();

    return (totalWritten >= 0) ? totalWritten : -1;
}

int yaffs_write(int fd, const void *buf, unsigned int nbyte)
{
    return yaffsfs_do_write(fd, buf, nbyte, 0, 0);
}

int yaffs_pwrite(int fd, const void *buf, unsigned int nbyte, Y_LOFF_T offset)
{
    return yaffsfs_do_write(fd, buf, nbyte, 1, offset);
}

int yaffs_truncate_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                          Y_LOFF_T new_size)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int result = YAFFS_FAIL;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);
    obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (obj->variant_type != YAFFS_OBJECT_TYPE_FILE)
        yaffsfs_SetError(-EISDIR);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (new_size < 0 || new_size > YAFFS_MAX_FILE_SIZE)
        yaffsfs_SetError(-EINVAL);
    else
        result = yaffs_resize_file(obj, new_size);

    yaffsfs_Unlock();

    return (result) ? 0 : -1;
}

int yaffs_truncate_reldev(struct yaffs_dev *dev, const YCHAR *path,
                          Y_LOFF_T new_size)
{
    return yaffs_truncate_reldir(ROOT_DIR(dev), path, new_size);
}

int yaffs_truncate(const YCHAR *path, Y_LOFF_T new_size)
{
    return yaffs_truncate_reldir(NULL, path, new_size);
}

int yaffs_ftruncate(int handle, Y_LOFF_T new_size)
{
    struct yaffsfs_FileDes *fd = NULL;
    struct yaffs_obj *obj = NULL;
    int result = 0;

    yaffsfs_Lock();
    fd = yaffsfs_HandleToFileDes(handle);
    obj = yaffsfs_HandleToObject(handle);

    if (!fd || !obj)
        /* bad handle */
        yaffsfs_SetError(-EBADF);
    else if (!fd->writing)
        yaffsfs_SetError(-EINVAL);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (new_size < 0 || new_size > YAFFS_MAX_FILE_SIZE)
        yaffsfs_SetError(-EINVAL);
    else
        /* resize the file */
        result = yaffs_resize_file(obj, new_size);
    yaffsfs_Unlock();

    return (result) ? 0 : -1;

}

Y_LOFF_T yaffs_lseek(int handle, Y_LOFF_T offset, int whence)
{
    struct yaffsfs_FileDes *fd = NULL;
    struct yaffs_obj *obj = NULL;
    Y_LOFF_T pos = -1;
    Y_LOFF_T fSize = -1;

    yaffsfs_Lock();
    fd = yaffsfs_HandleToFileDes(handle);
    obj = yaffsfs_HandleToObject(handle);

    if (!fd || !obj)
        yaffsfs_SetError(-EBADF);
    else if (offset > YAFFS_MAX_FILE_SIZE)
        yaffsfs_SetError(-EINVAL);
    else
    {
        if (whence == SEEK_SET)
        {
            if (offset >= 0)
                pos = offset;
        }
        else if (whence == SEEK_CUR)
        {
            if ((fd->v.position + offset) >= 0)
                pos = (fd->v.position + offset);
        }
        else if (whence == SEEK_END)
        {
            fSize = yaffs_get_obj_length(obj);
            if (fSize >= 0 && (fSize + offset) >= 0)
                pos = fSize + offset;
        }

        if (pos >= 0 && pos <= YAFFS_MAX_FILE_SIZE)
            fd->v.position = pos;
        else
        {
            yaffsfs_SetError(-EINVAL);
            pos = -1;
        }
    }

    yaffsfs_Unlock();

    return pos;
}

static int yaffsfs_DoUnlink_reldir(struct yaffs_obj *reldir,
                                   const YCHAR *path, int isDirectory)
{
    struct yaffs_obj *dir = NULL;
    struct yaffs_obj *obj = NULL;
    YCHAR *name;
    int result = YAFFS_FAIL;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 0, NULL, NULL, NULL);
    dir = yaffsfs_FindDirectory(reldir, path, &name, 0, &notDir, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir)
        yaffsfs_SetError(-ENOENT);
    else if (yaffs_strncmp(name, _Y("."), 2) == 0)
        yaffsfs_SetError(-EINVAL);
    else if (!obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (!isDirectory &&
             obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
        yaffsfs_SetError(-EISDIR);
    else if (isDirectory &&
             obj->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
        yaffsfs_SetError(-ENOTDIR);
    else if (isDirectory && obj == obj->my_dev->root_dir)
        yaffsfs_SetError(-EBUSY);   /* Can't rmdir a root */
    else
    {
        result = yaffs_unlinker(dir, name);

        if (result == YAFFS_FAIL && isDirectory)
            yaffsfs_SetError(-ENOTEMPTY);
    }

    yaffsfs_Unlock();

    return (result == YAFFS_FAIL) ? -1 : 0;
}

int yaffs_unlink_reldir(struct yaffs_obj *reldir, const YCHAR *path)
{
    return yaffsfs_DoUnlink_reldir(reldir, path, 0);
}

int yaffs_unlink_reldev(struct yaffs_dev *dev, const YCHAR *path)
{
    return yaffsfs_DoUnlink_reldir(ROOT_DIR(dev), path, 0);
}

int yaffs_unlink(const YCHAR *path)
{
    return yaffs_unlink_reldir(NULL, path);
}

int yaffs_funlink(int fd)
{
    struct yaffs_obj *obj;
    int retVal = -1;

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (!obj)
        yaffsfs_SetError(-EBADF);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY &&
             !(list_empty(&obj->variant.dir_variant.children)))
        yaffsfs_SetError(-ENOTEMPTY);
    else if (obj == obj->my_dev->root_dir)
        yaffsfs_SetError(-EBUSY);   /* Can't rmdir a root */
    else if (yaffs_unlink_obj(obj) == YAFFS_OK)
        retVal = 0;

    yaffsfs_Unlock();

    return retVal;
}

int yaffs_fgetfl(int fd, int *flags)
{
    struct yaffsfs_FileDes *fdp = yaffsfs_HandleToFileDes(fd);
    int retVal;

    yaffsfs_Lock();

    if (!flags || !fdp)
    {
        yaffsfs_SetError(-EINVAL);
        retVal = -1;
    }
    else
    {
        if (fdp->reading && fdp->writing)
            *flags = O_RDWR;
        else if (fdp->writing)
            *flags = O_WRONLY;
        else
            *flags = O_RDONLY;
        retVal = 0;
    }

    yaffsfs_Unlock();
    return retVal;
}


static int rename_file_over_dir(struct yaffs_obj *obj, struct yaffs_obj *newobj)
{
    if (obj && obj->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY &&
            newobj && newobj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
        return 1;
    else
        return 0;
}

static int rename_dir_over_file(struct yaffs_obj *obj, struct yaffs_obj *newobj)
{
    if (obj && obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY &&
            newobj && newobj->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
        return 1;
    else
        return 0;
}

int yaffs_rename_reldir(struct yaffs_obj *reldir,
                        const YCHAR *oldPath, const YCHAR *newPath)
{
    struct yaffs_obj *olddir = NULL;
    struct yaffs_obj *newdir = NULL;
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *newobj = NULL;
    YCHAR *oldname;
    YCHAR *newname;
    int result = YAFFS_FAIL;
    int rename_allowed = 1;
    int notOldDir = 0;
    int notNewDir = 0;
    int oldLoop = 0;
    int newLoop = 0;

    YCHAR *alt_newpath = NULL;

    if (yaffsfs_CheckMemRegion(oldPath, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(newPath, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(oldPath) < 0 || yaffsfs_CheckPath(newPath) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    if (yaffsfs_alt_dir_path(newPath, &alt_newpath) < 0)
    {
        yaffsfs_SetError(-ENOMEM);
        return -1;
    }
    if (alt_newpath)
        newPath = alt_newpath;

    yaffsfs_Lock();

    olddir = yaffsfs_FindDirectory(reldir, oldPath, &oldname, 0,
                                   &notOldDir, &oldLoop);
    newdir = yaffsfs_FindDirectory(reldir, newPath, &newname, 0,
                                   &notNewDir, &newLoop);
    obj = yaffsfs_FindObject(reldir, oldPath, 0, 0, NULL, NULL, NULL);
    newobj = yaffsfs_FindObject(reldir, newPath, 0, 0, NULL, NULL, NULL);

    /* If the object being renamed is a directory and the
     * path ended with a "/" then the olddir == obj.
     * We pass through NULL for the old name to tell the lower layers
     * to use olddir as the object.
     */

    if (olddir == obj)
        oldname = NULL;

    if ((!olddir && notOldDir) || (!newdir && notNewDir))
    {
        yaffsfs_SetError(-ENOTDIR);
        rename_allowed = 0;
    }
    else if (oldLoop || newLoop)
    {
        yaffsfs_SetError(-ELOOP);
        rename_allowed = 0;
    }
    else if (olddir && oldname &&
             yaffs_strncmp(oldname, _Y("."), 2) == 0)
    {
        yaffsfs_SetError(-EINVAL);
        rename_allowed = 0;
    }
    else if (!olddir || !newdir || !obj)
    {
        yaffsfs_SetError(-ENOENT);
        rename_allowed = 0;
    }
    else if (obj->my_dev->read_only)
    {
        yaffsfs_SetError(-EROFS);
        rename_allowed = 0;
    }
    else if (rename_file_over_dir(obj, newobj))
    {
        yaffsfs_SetError(-EISDIR);
        rename_allowed = 0;
    }
    else if (rename_dir_over_file(obj, newobj))
    {
        yaffsfs_SetError(-ENOTDIR);
        rename_allowed = 0;
    }
    else if (yaffs_is_non_empty_dir(newobj))
    {
        yaffsfs_SetError(-ENOTEMPTY);
        rename_allowed = 0;
    }
    else if (olddir->my_dev != newdir->my_dev)
    {
        /* Rename must be on same device */
        yaffsfs_SetError(-EXDEV);
        rename_allowed = 0;
    }
    else if (obj && obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
    {
        /*
         * It is a directory, check that it is not being renamed to
         * being its own decendent.
         * Do this by tracing from the new directory back to the root,
         * checking for obj
         */

        struct yaffs_obj *xx = newdir;

        while (rename_allowed && xx)
        {
            if (xx == obj)
                rename_allowed = 0;
            xx = xx->parent;
        }
        if (!rename_allowed)
            yaffsfs_SetError(-EINVAL);
    }

    if (rename_allowed)
        result = yaffs_rename_obj(olddir, oldname, newdir, newname);

    yaffsfs_Unlock();

    kfree(alt_newpath);

    return (result == YAFFS_FAIL) ? -1 : 0;
}

int yaffs_rename_reldev(struct yaffs_dev *dev, const YCHAR *oldPath,
                        const YCHAR *newPath)
{
    return yaffs_rename_reldir(ROOT_DIR(dev), oldPath, newPath);
}
int yaffs_rename(const YCHAR *oldPath, const YCHAR *newPath)
{
    return yaffs_rename_reldir(NULL, oldPath, newPath);
}

static int yaffsfs_DoStat(struct yaffs_obj *obj, struct yaffs_stat *buf)
{
    int retVal = -1;

    obj = yaffs_get_equivalent_obj(obj);

    if (obj && buf)
    {
        buf->st_dev = 0;
        buf->st_ino = obj->obj_id;
        buf->st_mode = obj->yst_mode & ~S_IFMT;

        if (obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
            buf->st_mode |= S_IFDIR;
        else if (obj->variant_type == YAFFS_OBJECT_TYPE_SYMLINK)
            buf->st_mode |= S_IFLNK;
        else if (obj->variant_type == YAFFS_OBJECT_TYPE_FILE)
            buf->st_mode |= S_IFREG;

        buf->st_nlink = yaffs_get_obj_link_count(obj);
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_rdev = obj->yst_rdev;
        buf->st_size = yaffs_get_obj_length(obj);
        buf->st_blksize = obj->my_dev->data_bytes_per_chunk;
        buf->st_blocks = (buf->st_size + buf->st_blksize - 1) /
                         buf->st_blksize;
#if CONFIG_YAFFS_WINCE
        buf->yst_wince_atime[0] = obj->win_atime[0];
        buf->yst_wince_atime[1] = obj->win_atime[1];
        buf->yst_wince_ctime[0] = obj->win_ctime[0];
        buf->yst_wince_ctime[1] = obj->win_ctime[1];
        buf->yst_wince_mtime[0] = obj->win_mtime[0];
        buf->yst_wince_mtime[1] = obj->win_mtime[1];
#else
        buf->yst_atime = obj->yst_atime;
        buf->yst_ctime = obj->yst_ctime;
        buf->yst_mtime = obj->yst_mtime;
#endif
        retVal = 0;
    }
    return retVal;
}

static int yaffsfs_DoStatOrLStat_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                                        struct yaffs_stat *buf, int doLStat)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(buf, sizeof(*buf), 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (!doLStat && obj)
        obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
        retVal = yaffsfs_DoStat(obj, buf);

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_stat_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                      struct yaffs_stat *buf)
{
    return yaffsfs_DoStatOrLStat_reldir(reldir, path, buf, 0);
}

int yaffs_lstat_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                       struct yaffs_stat *buf)
{
    return yaffsfs_DoStatOrLStat_reldir(reldir, path, buf, 1);
}

int yaffs_stat_reldev(struct yaffs_dev *dev, const YCHAR *path,
                      struct yaffs_stat *buf)
{
    return yaffsfs_DoStatOrLStat_reldir(ROOT_DIR(dev), path, buf, 0);
}

int yaffs_lstat_reldev(struct yaffs_dev *dev, const YCHAR *path,
                       struct yaffs_stat *buf)
{
    return yaffsfs_DoStatOrLStat_reldir(ROOT_DIR(dev), path, buf, 1);
}

int yaffs_stat(const YCHAR *path, struct yaffs_stat *buf)
{
    return yaffs_stat_reldir(NULL, path, buf);
}

int yaffs_lstat(const YCHAR *path, struct yaffs_stat *buf)
{
    return yaffs_lstat_reldir(NULL, path, buf);
}

int yaffs_fstat(int fd, struct yaffs_stat *buf)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(buf, sizeof(*buf), 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
        retVal = yaffsfs_DoStat(obj, buf);
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

static int yaffsfs_DoUtime(struct yaffs_obj *obj,
                           const struct yaffs_utimbuf *buf)
{
    int retVal = -1;
    struct yaffs_utimbuf local;

    obj = yaffs_get_equivalent_obj(obj);

    if (obj && obj->my_dev->read_only)
    {
        yaffsfs_SetError(-EROFS);
        return -1;
    }

#if !CONFIG_YAFFS_WINCE
    // if the the buffer is null then create one with the fields set to the current time.
    if (!buf)
    {
        local.actime = Y_CURRENT_TIME;
        local.modtime = local.actime;
        buf = &local;
    }

    // copy the buffer's time into the obj.
    if (obj)
    {
        int result;

        obj->yst_atime = buf->actime;
        obj->yst_mtime = buf->modtime;

        // set the obj to dirty to cause it to be written to flash during the next flush operation.
        obj->dirty = 1;
        result = yaffs_flush_file(obj, 0, 0, 0);
        retVal = result == YAFFS_OK ? 0 : -1;
    }
#endif

    return retVal;
}

int yaffs_utime_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                       const struct yaffs_utimbuf *buf)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (!path)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
        retVal = yaffsfs_DoUtime(obj, buf);

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_utime_reldev(struct yaffs_dev *dev, const YCHAR *path,
                       const struct yaffs_utimbuf *buf)
{
    return yaffs_utime_reldir(ROOT_DIR(dev), path, buf);
}

int yaffs_utime(const YCHAR *path, const struct yaffs_utimbuf *buf)
{
    return yaffs_utime_reldir(NULL, path, buf);
}

int yaffs_futime(int fd, const struct yaffs_utimbuf *buf)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
        retVal = yaffsfs_DoUtime(obj, buf);
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

#ifndef CONFIG_YAFFS_WINCE
/* xattrib functions */

static int yaffs_do_setxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                                    const char *name, const void *data, int size,
                                    int flags, int follow)
{
    struct yaffs_obj *obj;
    struct yaffs_obj *dir;
    int notDir = 0;
    int loop = 0;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(name, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(data, size, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (follow)
        obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
    {
        retVal = yaffs_set_xattrib(obj, name, data, size, flags);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_setxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                          const char *name, const void *data, int size, int flags)
{
    return yaffs_do_setxattr_reldir(reldir, path, name, data, size, flags, 1);
}

int yaffs_setxattr_reldev(struct yaffs_dev *dev, const YCHAR *path,
                          const char *name, const void *data, int size, int flags)
{
    return yaffs_setxattr_reldir(ROOT_DIR(dev), path, name, data, size, flags);
}

int yaffs_setxattr(const YCHAR *path, const char *name,
                   const void *data, int size, int flags)
{
    return yaffs_setxattr_reldir(NULL, path, name, data, size, flags);
}

int yaffs_lsetxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path, const char *name,
                           const void *data, int size, int flags)
{
    return yaffs_do_setxattr_reldir(reldir, path, name, data, size, flags, 0);
}

int yaffs_lsetxattr_reldev(struct yaffs_dev *dev, const YCHAR *path, const char *name,
                           const void *data, int size, int flags)
{
    return yaffs_lsetxattr_reldir(ROOT_DIR(dev), path, name, data, size, flags);
}

int yaffs_lsetxattr(const YCHAR *path, const char *name,
                    const void *data, int size, int flags)
{
    return yaffs_lsetxattr_reldir(NULL, path, name, data, size, flags);
}

int yaffs_fsetxattr(int fd, const char *name,
                    const void *data, int size, int flags)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(name, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(data, size, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (!obj)
        yaffsfs_SetError(-EBADF);
    else
    {
        retVal = yaffs_set_xattrib(obj, name, data, size, flags);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }

    yaffsfs_Unlock();

    return retVal;
}

static int yaffs_do_getxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                                    const char *name, void *data, int size, int follow)
{
    struct yaffs_obj *obj;
    struct yaffs_obj *dir;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(name, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(data, size, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (follow)
        obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
    {
        retVal = yaffs_get_xattrib(obj, name, data, size);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }
    yaffsfs_Unlock();

    return retVal;

}

int yaffs_getxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                          const char *name, void *data, int size)
{
    return yaffs_do_getxattr_reldir(reldir, path, name, data, size, 1);
}

int yaffs_getxattr_reldev(struct yaffs_dev *dev, const YCHAR *path, const char *name, void *data, int size)
{
    return yaffs_getxattr_reldir(ROOT_DIR(dev), path, name, data, size);
}

int yaffs_getxattr(const YCHAR *path, const char *name, void *data, int size)
{
    return yaffs_getxattr_reldir(NULL, path, name, data, size);
}

int yaffs_lgetxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                           const char *name, void *data, int size)
{
    return yaffs_do_getxattr_reldir(reldir, path, name, data, size, 0);
}

int yaffs_lgetxattr_reldev(struct yaffs_dev *dev, const YCHAR *path, const char *name, void *data, int size)
{
    return yaffs_lgetxattr_reldir(ROOT_DIR(dev), path, name, data, size);
}

int yaffs_lgetxattr(const YCHAR *path, const char *name, void *data, int size)
{
    return yaffs_lgetxattr_reldir(NULL, path, name, data, size);
}

int yaffs_fgetxattr(int fd, const char *name, void *data, int size)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(name, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(data, size, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
    {
        retVal = yaffs_get_xattrib(obj, name, data, size);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

static int yaffs_do_listxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                                     char *data, int size, int follow)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(data, size, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (follow)
        obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
    {
        retVal = yaffs_list_xattrib(obj, data, size);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_listxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                           char *data, int size)
{
    return yaffs_do_listxattr_reldir(reldir, path, data, size, 1);
}

int yaffs_listxattr_reldev(struct yaffs_dev *dev, const YCHAR *path, char *data, int size)
{
    return yaffs_listxattr_reldir(ROOT_DIR(dev), path, data, size);
}

int yaffs_listxattr(const YCHAR *path, char *data, int size)
{
    return yaffs_listxattr_reldir(NULL, path, data, size);
}

int yaffs_llistxattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                            char *data, int size)
{
    return yaffs_do_listxattr_reldir(reldir, path, data, size, 0);
}

int yaffs_llistxattr_reldev(struct yaffs_dev *dev, const YCHAR *path, char *data, int size)
{
    return yaffs_llistxattr_reldir(ROOT_DIR(dev), path, data, size);
}

int yaffs_llistxattr(const YCHAR *path, char *data, int size)
{
    return yaffs_llistxattr_reldir(NULL, path, data, size);
}

int yaffs_flistxattr(int fd, char *data, int size)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(data, size, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
    {
        retVal = yaffs_list_xattrib(obj, data, size);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

static int yaffs_do_removexattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                                       const char *name, int follow)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int notDir = 0;
    int loop = 0;
    int retVal = -1;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(name, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (follow)
        obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else
    {
        retVal = yaffs_remove_xattrib(obj, name);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_removexattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                             const char *name)
{
    return yaffs_do_removexattr_reldir(reldir, path, name, 1);
}

int yaffs_removexattr_reldev(struct yaffs_dev *dev, const YCHAR *path, const char *name)
{
    return yaffs_removexattr_reldir(ROOT_DIR(dev), path, name);
}

int yaffs_removexattr(const YCHAR *path, const char *name)
{
    return yaffs_removexattr_reldir(NULL, path, name);
}

int yaffs_lremovexattr_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                              const char *name)
{
    return yaffs_do_removexattr_reldir(reldir, path, name, 0);
}

int yaffs_lremovexattr_reldev(struct yaffs_dev *dev, const YCHAR *path, const char *name)
{
    return yaffs_lremovexattr_reldir(ROOT_DIR(dev), path, name);
}

int yaffs_lremovexattr(const YCHAR *path, const char *name)
{
    return yaffs_lremovexattr_reldir(NULL, path, name);
}

int yaffs_fremovexattr(int fd, const char *name)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    if (yaffsfs_CheckMemRegion(name, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
    {
        retVal = yaffs_remove_xattrib(obj, name);
        if (retVal < 0)
        {
            yaffsfs_SetError(retVal);
            retVal = -1;
        }
    }
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}
#endif

#ifdef CONFIG_YAFFS_WINCE
int yaffs_get_wince_times(int fd, unsigned *wctime,
                          unsigned *watime, unsigned *wmtime)
{
    struct yaffs_obj *obj;

    int retVal = -1;

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
    {

        if (wctime)
        {
            wctime[0] = obj->win_ctime[0];
            wctime[1] = obj->win_ctime[1];
        }
        if (watime)
        {
            watime[0] = obj->win_atime[0];
            watime[1] = obj->win_atime[1];
        }
        if (wmtime)
        {
            wmtime[0] = obj->win_mtime[0];
            wmtime[1] = obj->win_mtime[1];
        }

        retVal = 0;
    }
    else
        /*  bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

int yaffs_set_wince_times(int fd,
                          const unsigned *wctime,
                          const unsigned *watime, const unsigned *wmtime)
{
    struct yaffs_obj *obj;
    int result;
    int retVal = -1;

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (obj)
    {

        if (wctime)
        {
            obj->win_ctime[0] = wctime[0];
            obj->win_ctime[1] = wctime[1];
        }
        if (watime)
        {
            obj->win_atime[0] = watime[0];
            obj->win_atime[1] = watime[1];
        }
        if (wmtime)
        {
            obj->win_mtime[0] = wmtime[0];
            obj->win_mtime[1] = wmtime[1];
        }

        obj->dirty = 1;
        result = yaffs_flush_file(obj, 0, 0, 0);
        retVal = 0;
    }
    else
        /* bad handle */
        yaffsfs_SetError(-EBADF);

    yaffsfs_Unlock();

    return retVal;
}

#endif

static int yaffsfs_DoChMod(struct yaffs_obj *obj, mode_t mode)
{
    int result = -1;

    if (obj)
        obj = yaffs_get_equivalent_obj(obj);

    if (obj)
    {
        obj->yst_mode = mode;
        obj->dirty = 1;
        result = yaffs_flush_file(obj, 0, 0, 0);
    }

    return result == YAFFS_OK ? 0 : -1;
}

int yaffs_access_reldir(struct yaffs_obj *reldir, const YCHAR *path, int amode)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int notDir = 0;
    int loop = 0;
    int retval = -1;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    if (amode & ~(R_OK | W_OK | X_OK))
    {
        yaffsfs_SetError(-EINVAL);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);
    obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else if ((amode & W_OK) && obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else
    {
        int access_ok = 1;

        if ((amode & R_OK) && !(obj->yst_mode & S_IRUSR))
            access_ok = 0;
        if ((amode & W_OK) && !(obj->yst_mode & S_IWUSR))
            access_ok = 0;
        if ((amode & X_OK) && !(obj->yst_mode & S_IXUSR))
            access_ok = 0;

        if (!access_ok)
            yaffsfs_SetError(-EACCES);
        else
            retval = 0;
    }

    yaffsfs_Unlock();

    return retval;

}

int yaffs_access_reldev(struct yaffs_dev *dev, const YCHAR *path, int amode)
{
    return yaffs_access_reldir(ROOT_DIR(dev), path, amode);
}

int yaffs_access(const YCHAR *path, int amode)
{
    return yaffs_access_reldir(NULL, path, amode);
}

int yaffs_chmod_reldir(struct yaffs_obj *reldir, const YCHAR *path, mode_t mode)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    if (mode & ~(0777))
    {
        yaffsfs_SetError(-EINVAL);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);
    obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else
        retVal = yaffsfs_DoChMod(obj, mode);

    yaffsfs_Unlock();

    return retVal;

}

int yaffs_chmod_reldev(struct yaffs_dev *dev, const YCHAR *path, mode_t mode)
{
    return yaffs_chmod_reldir(ROOT_DIR(dev), path, mode);
}

int yaffs_chmod(const YCHAR *path, mode_t mode)
{
    return yaffs_chmod_reldir(NULL, path, mode);
}

int yaffs_fchmod(int fd, mode_t mode)
{
    struct yaffs_obj *obj;
    int retVal = -1;

    if (mode & ~(0777))
    {
        yaffsfs_SetError(-EINVAL);
        return -1;
    }

    yaffsfs_Lock();
    obj = yaffsfs_HandleToObject(fd);

    if (!obj)
        yaffsfs_SetError(-EBADF);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else
        retVal = yaffsfs_DoChMod(obj, mode);

    yaffsfs_Unlock();

    return retVal;
}

int yaffs_mkdir_reldir(struct yaffs_obj *reldir, const YCHAR *path, mode_t mode)
{
    struct yaffs_obj *parent = NULL;
    struct yaffs_obj *dir = NULL;
    YCHAR *name;
    YCHAR *alt_path = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    if (yaffsfs_alt_dir_path(path, &alt_path) < 0)
    {
        yaffsfs_SetError(-ENOMEM);
        return -1;
    }
    if (alt_path)
        path = alt_path;

    yaffsfs_Lock();
    parent = yaffsfs_FindDirectory(reldir, path, &name, 0, &notDir, &loop);
    if (!parent && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!parent)
        yaffsfs_SetError(-ENOENT);
    else if (yaffsfs_TooManyObjects(parent->my_dev))
        yaffsfs_SetError(-ENFILE);
    else if (yaffs_strnlen(name, 5) == 0)
    {
        /* Trying to make the root itself */
        yaffsfs_SetError(-EEXIST);
    }
    else if (parent->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else
    {
        dir = yaffs_create_dir(parent, name, mode, 0, 0);
        if (dir)
            retVal = 0;
        else if (yaffs_find_by_name(parent, name))
            yaffsfs_SetError(-EEXIST);  /* name exists */
        else
            yaffsfs_SetError(-ENOSPC);  /* assume no space */
    }

    yaffsfs_Unlock();

    kfree(alt_path);

    return retVal;
}

int yaffs_mkdir_reldev(struct yaffs_dev *dev, const YCHAR *path, mode_t mode)
{
    return yaffs_mkdir_reldir(ROOT_DIR(dev), path, mode);
}

int yaffs_mkdir(const YCHAR *path, mode_t mode)
{
    return yaffs_mkdir_reldir(NULL, path, mode);
}

int yaffs_rmdir_reldir(struct yaffs_obj *reldir, const YCHAR *path)
{
    int result;
    YCHAR *alt_path;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    if (yaffsfs_alt_dir_path(path, &alt_path) < 0)
    {
        yaffsfs_SetError(-ENOMEM);
        return -1;
    }
    if (alt_path)
        path = alt_path;
    result = yaffsfs_DoUnlink_reldir(reldir, path, 1);

    kfree(alt_path);

    return result;
}

int yaffs_rmdir_reldev(struct yaffs_dev *dev, const YCHAR *path)
{
    return yaffs_rmdir_reldir(ROOT_DIR(dev), path);
}

int yaffs_rmdir(const YCHAR *path)
{
    return yaffs_rmdir_reldir(NULL, path);
}

/*
 * The mount/unmount/sync functions act on devices rather than reldirs.
 */
void *yaffs_getdev(const YCHAR *path)
{
    struct yaffs_dev *dev = NULL;
    YCHAR *dummy;
    dev = yaffsfs_FindDevice(path, &dummy);
    return (void *)dev;
}

int yaffs_mount_common(struct yaffs_dev *dev, const YCHAR *path,
                       int read_only, int skip_checkpt)
{
    int retVal = -1;
    int result = YAFFS_FAIL;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        yaffs_trace(YAFFS_TRACE_MOUNT, "yaffs: Mounting %s", path);

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();

    yaffsfs_InitHandles();

    if (!dev)
        dev = yaffsfs_FindMountPoint(path);

    if (dev)
    {
        if (!dev->is_mounted)
        {
            dev->read_only = read_only ? 1 : 0;
            if (skip_checkpt)
            {
                u8 skip = dev->param.skip_checkpt_rd;
                dev->param.skip_checkpt_rd = 1;
                result = yaffs_guts_initialise(dev);
                dev->param.skip_checkpt_rd = skip;
            }
            else
            {
                result = yaffs_guts_initialise(dev);
            }

            if (result == YAFFS_FAIL)
                yaffsfs_SetError(-ENOMEM);
            retVal = result ? 0 : -1;

        }
        else
            yaffsfs_SetError(-EBUSY);
    }
    else
        yaffsfs_SetError(-ENODEV);

    yaffsfs_Unlock();
    return retVal;

}

int yaffs_mount3_reldev(struct yaffs_dev *dev, int read_only, int skip_checkpt)
{
    return yaffs_mount_common(dev, NULL, read_only, skip_checkpt);
}

int yaffs_mount3(const YCHAR *path, int read_only, int skip_checkpt)
{
    return yaffs_mount_common(NULL, path, read_only, skip_checkpt);
}

int yaffs_mount2_reldev(struct yaffs_dev *dev, int readonly)
{
    return yaffs_mount_common(dev, NULL, readonly, 0);
}

int yaffs_mount2(const YCHAR *path, int readonly)
{
    return yaffs_mount_common(NULL, path, readonly, 0);
}

int yaffs_mount_reldev(struct yaffs_dev *dev)
{
    return yaffs_mount_common(dev, NULL, 0, 0);
}

int yaffs_mount(const YCHAR *path)
{
    return yaffs_mount_common(NULL, path, 0, 0);
}

static int yaffs_sync_common(struct yaffs_dev *dev,
                             const YCHAR *path,
                             int do_checkpt)
{
    int retVal = -1;
    YCHAR *dummy;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindDevice(path, &dummy);

    if (dev)
    {
        if (!dev->is_mounted)
            yaffsfs_SetError(-EINVAL);
        else if (dev->read_only)
            yaffsfs_SetError(-EROFS);
        else
        {

            yaffs_flush_whole_cache(dev, 0);
            if (do_checkpt)
                yaffs_checkpoint_save(dev);
            retVal = 0;

        }
    }
    else
        yaffsfs_SetError(-ENODEV);

    yaffsfs_Unlock();
    return retVal;
}

int yaffs_sync_files_reldev(struct yaffs_dev *dev)
{
    return yaffs_sync_common(dev, NULL, 0);
}

int yaffs_sync_files(const YCHAR *path)
{
    return yaffs_sync_common(NULL, path, 0);
}

int yaffs_sync_reldev(struct yaffs_dev *dev)
{
    return yaffs_sync_common(dev, NULL, 1);
}

int yaffs_sync(const YCHAR *path)
{
    return yaffs_sync_common(NULL, path, 1);
}


static int yaffsfs_bg_gc_common(struct yaffs_dev *dev,
                                const YCHAR *path,
                                int urgency)
{
    int retVal = -1;
    YCHAR *dummy;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindDevice(path, &dummy);

    if (dev)
    {
        if (!dev->is_mounted)
            yaffsfs_SetError(-EINVAL);
        else
            retVal = yaffs_bg_gc(dev, urgency);
    }
    else
        yaffsfs_SetError(-ENODEV);

    yaffsfs_Unlock();
    return retVal;
}

/* Background gc functions.
 * These return 0 when bg done or greater than 0 when gc has been
 * done and there is still a lot of garbage to be cleaned up.
 */

int yaffs_do_background_gc(const YCHAR *path, int urgency)
{
    return yaffsfs_bg_gc_common(NULL, path, urgency);
}

int yaffs_do_background_gc_reldev(struct yaffs_dev *dev, int urgency)
{
    return yaffsfs_bg_gc_common(dev, NULL, urgency);
}

static int yaffsfs_IsDevBusy(struct yaffs_dev *dev)
{
    int i;
    struct yaffs_obj *obj;

    for (i = 0; i < YAFFSFS_N_HANDLES; i++)
    {
        obj = yaffsfs_HandleToObject(i);
        if (obj && obj->my_dev == dev)
            return 1;
    }
    return 0;
}

int yaffs_remount_common(struct yaffs_dev *dev, const YCHAR *path,
                         int force, int read_only)
{
    int retVal = -1;
    int was_read_only;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindMountPoint(path);

    if (dev)
    {
        if (dev->is_mounted)
        {
            yaffs_flush_whole_cache(dev, 0);

            if (force || !yaffsfs_IsDevBusy(dev))
            {
                if (read_only)
                    yaffs_checkpoint_save(dev);
                was_read_only = dev->read_only;
                dev->read_only = read_only ? 1 : 0;
                if (was_read_only && !read_only)
                {
                    yaffs_guts_cleanup(dev);
                }
                retVal = 0;
            }
            else
                yaffsfs_SetError(-EBUSY);

        }
        else
            yaffsfs_SetError(-EINVAL);

    }
    else
        yaffsfs_SetError(-ENODEV);

    yaffsfs_Unlock();
    return retVal;

}

int yaffs_remount_reldev(struct yaffs_dev *dev, int force, int read_only)
{
    return yaffs_remount_common(dev, NULL, force, read_only);
}
int yaffs_remount(const YCHAR *path, int force, int read_only)
{
    return yaffs_remount_common(NULL, path, force, read_only);
}

int yaffs_unmount2_common(struct yaffs_dev *dev, const YCHAR *path, int force)
{
    int retVal = -1;


    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindMountPoint(path);

    if (dev)
    {
        if (dev->is_mounted)
        {
            int inUse;
            yaffs_flush_whole_cache(dev, 0);
            yaffs_checkpoint_save(dev);
            inUse = yaffsfs_IsDevBusy(dev);
            if (!inUse || force)
            {
                if (inUse)
                    yaffsfs_BreakDeviceHandles(dev);
                yaffs_deinitialise(dev);

                retVal = 0;
            }
            else
                yaffsfs_SetError(-EBUSY);

        }
        else
            yaffsfs_SetError(-EINVAL);

    }
    else
        yaffsfs_SetError(-ENODEV);

    yaffsfs_Unlock();
    return retVal;

}

int yaffs_unmount2_reldev(struct yaffs_dev *dev, int force)
{
    return yaffs_unmount2_common(dev, NULL, force);
}

int yaffs_unmount2(const YCHAR *path, int force)
{
    return yaffs_unmount2_common(NULL, path, force);
}

int yaffs_unmount_reldev(struct yaffs_dev *dev)
{
    return yaffs_unmount2_reldev(dev, 0);
}

int yaffs_unmount(const YCHAR *path)
{
    return yaffs_unmount2(path, 0);
}

int yaffs_format_common(struct yaffs_dev *dev,
                        const YCHAR *path,
                        int unmount_flag,
                        int force_unmount_flag,
                        int remount_flag)
{
    int retVal = 0;
    int result;

    if (!dev)
    {
        if (!path)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindMountPoint(path);

    if (dev)
    {
        int was_mounted = dev->is_mounted;

        if (dev->is_mounted && unmount_flag)
        {
            int inUse;
            yaffs_flush_whole_cache(dev, 0);
            yaffs_checkpoint_save(dev);
            inUse = yaffsfs_IsDevBusy(dev);
            if (!inUse || force_unmount_flag)
            {
                if (inUse)
                    yaffsfs_BreakDeviceHandles(dev);
                yaffs_deinitialise(dev);
            }
        }

        if (dev->is_mounted)
        {
            yaffsfs_SetError(-EBUSY);
            retVal = -1;
        }
        else
        {
            yaffs_guts_format_dev(dev);
            if (was_mounted && remount_flag)
            {
                result = yaffs_guts_initialise(dev);
                if (result == YAFFS_FAIL)
                {
                    yaffsfs_SetError(-ENOMEM);
                    retVal = -1;
                }
            }
        }
    }
    else
    {
        yaffsfs_SetError(-ENODEV);
        retVal = -1;
    }

    yaffsfs_Unlock();
    return retVal;

}

int yaffs_format_reldev(struct yaffs_dev *dev,
                        int unmount_flag,
                        int force_unmount_flag,
                        int remount_flag)
{
    return yaffs_format_common(dev, NULL, unmount_flag,
                               force_unmount_flag, remount_flag);
}

int yaffs_format(const YCHAR *path,
                 int unmount_flag,
                 int force_unmount_flag,
                 int remount_flag)
{
    return yaffs_format_common(NULL, path, unmount_flag,
                               force_unmount_flag, remount_flag);
}

Y_LOFF_T yaffs_freespace_common(struct yaffs_dev *dev, const YCHAR *path)
{
    Y_LOFF_T retVal = -1;
    YCHAR *dummy;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindDevice(path, &dummy);
    if (dev && dev->is_mounted)
    {
        retVal = yaffs_get_n_free_chunks(dev);
        retVal *= dev->data_bytes_per_chunk;

    }
    else
        yaffsfs_SetError(-EINVAL);

    yaffsfs_Unlock();
    return retVal;
}

Y_LOFF_T yaffs_freespace_reldev(struct yaffs_dev *dev)
{
    return yaffs_freespace_common(dev, NULL);
}

Y_LOFF_T yaffs_freespace(const YCHAR *path)
{
    return yaffs_freespace_common(NULL, path);
}

Y_LOFF_T yaffs_totalspace_common(struct yaffs_dev *dev, const YCHAR *path)
{
    Y_LOFF_T retVal = -1;
    YCHAR *dummy;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindDevice(path, &dummy);
    if (dev && dev->is_mounted)
    {
        retVal = (dev->param.end_block - dev->param.start_block + 1) -
                 dev->param.n_reserved_blocks;
        retVal *= dev->param.chunks_per_block;
        retVal *= dev->data_bytes_per_chunk;

    }
    else
        yaffsfs_SetError(-EINVAL);

    yaffsfs_Unlock();
    return retVal;
}

Y_LOFF_T yaffs_totalspace_reldev(struct yaffs_dev *dev)
{
    return yaffs_totalspace_common(dev, NULL);
}

Y_LOFF_T yaffs_totalspace(const YCHAR *path)
{
    return yaffs_totalspace_common(NULL, path);
}

int yaffs_inodecount_common(struct yaffs_dev *dev, const YCHAR *path)
{
    Y_LOFF_T retVal = -1;
    YCHAR *dummy;

    if (!dev)
    {
        if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
        {
            yaffsfs_SetError(-EFAULT);
            return -1;
        }

        if (yaffsfs_CheckPath(path) < 0)
        {
            yaffsfs_SetError(-ENAMETOOLONG);
            return -1;
        }
    }

    yaffsfs_Lock();
    if (!dev)
        dev = yaffsfs_FindDevice(path, &dummy);
    if (dev && dev->is_mounted)
    {
        int n_obj = dev->n_obj;
        if (n_obj > dev->n_hardlinks)
            retVal = n_obj - dev->n_hardlinks;
    }

    if (retVal < 0)
        yaffsfs_SetError(-EINVAL);

    yaffsfs_Unlock();
    return retVal;
}

int yaffs_inodecount_reldev(struct yaffs_dev *dev)
{
    return yaffs_inodecount_common(dev, NULL);
}

int yaffs_inodecount(const YCHAR *path)
{
    return yaffs_inodecount_common(NULL, path);
}

void yaffs_add_device(struct yaffs_dev *dev)
{
    struct list_head *cfg;
    /* First check that the device is not in the list. */

    list_for_each(cfg, &yaffsfs_deviceList)
    {
        if (dev == list_entry(cfg, struct yaffs_dev, dev_list))
            return;
    }

    dev->is_mounted = 0;
    dev->param.remove_obj_fn = yaffsfs_RemoveObjectCallback;

    if (!dev->dev_list.next)
        INIT_LIST_HEAD(&dev->dev_list);

    list_add(&dev->dev_list, &yaffsfs_deviceList);


}

void yaffs_remove_device(struct yaffs_dev *dev)
{
    list_del_init(&dev->dev_list);
}

/* Functions to iterate through devices. NB Use with extreme care! */

static struct list_head *dev_iterator;
void yaffs_dev_rewind(void)
{
    dev_iterator = yaffsfs_deviceList.next;
}

struct yaffs_dev *yaffs_next_dev(void)
{
    struct yaffs_dev *retval;

    if (!dev_iterator)
        return NULL;
    if (dev_iterator == &yaffsfs_deviceList)
        return NULL;

    retval = list_entry(dev_iterator, struct yaffs_dev, dev_list);
    dev_iterator = dev_iterator->next;
    return retval;
}

/* Directory search stuff. */

static struct list_head search_contexts;

static void yaffsfs_SetDirRewound(struct yaffsfs_DirSearchContext *dsc)
{
    if (dsc &&
            dsc->dirObj &&
            dsc->dirObj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
    {

        dsc->offset = 0;

        if (list_empty(&dsc->dirObj->variant.dir_variant.children))
            dsc->nextReturn = NULL;
        else
            dsc->nextReturn =
                list_entry(dsc->dirObj->variant.dir_variant.
                           children.next, struct yaffs_obj,
                           siblings);
    }
    else
    {
        /* Hey someone isn't playing nice! */
    }
}

static void yaffsfs_DirAdvance(struct yaffsfs_DirSearchContext *dsc)
{
    if (dsc &&
            dsc->dirObj &&
            dsc->dirObj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
    {

        if (dsc->nextReturn == NULL ||
                list_empty(&dsc->dirObj->variant.dir_variant.children))
            dsc->nextReturn = NULL;
        else
        {
            struct list_head *next = dsc->nextReturn->siblings.next;

            if (next == &dsc->dirObj->variant.dir_variant.children)
                dsc->nextReturn = NULL; /* end of list */
            else
                dsc->nextReturn = list_entry(next,
                                             struct yaffs_obj,
                                             siblings);
        }
    }
    else
    {
        /* Hey someone isn't playing nice! */
    }
}

static void yaffsfs_RemoveObjectCallback(struct yaffs_obj *obj)
{

    struct list_head *i;
    struct yaffsfs_DirSearchContext *dsc;

    /* if search contexts not initilised then skip */
    if (!search_contexts.next)
        return;

    /* Iterate through the directory search contexts.
     * If any are the one being removed, then advance the dsc to
     * the next one to prevent a hanging ptr.
     */
    list_for_each(i, &search_contexts)
    {
        if (i)
        {
            dsc = list_entry(i, struct yaffsfs_DirSearchContext,
                             others);
            if (dsc->nextReturn == obj)
                yaffsfs_DirAdvance(dsc);
        }
    }

}

static yaffs_DIR *yaffsfs_opendir_reldir_no_lock(struct yaffs_obj *reldir,
        const YCHAR *dirname)
{
    yaffs_DIR *dir = NULL;
    struct yaffs_obj *obj = NULL;
    struct yaffsfs_DirSearchContext *dsc = NULL;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(dirname, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return NULL;
    }

    if (yaffsfs_CheckPath(dirname) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return NULL;
    }

    obj = yaffsfs_FindObject(reldir, dirname, 0, 1, NULL, &notDir, &loop);
    obj = yaffsfs_FollowLink(obj, 0, &loop);

    if (!obj && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
        yaffsfs_SetError(-ENOTDIR);
    else
    {
        int i;

        for (i = 0, dsc = NULL; i < YAFFSFS_N_DSC && !dsc; i++)
        {
            if (!yaffsfs_dsc[i].inUse)
                dsc = &yaffsfs_dsc[i];
        }

        dir = (yaffs_DIR *) dsc;

        if (dsc)
        {
            memset(dsc, 0, sizeof(struct yaffsfs_DirSearchContext));
            dsc->inUse = 1;
            dsc->dirObj = obj;
            yaffs_strncpy(dsc->name, dirname, NAME_MAX);
            INIT_LIST_HEAD(&dsc->others);

            if (!search_contexts.next)
                INIT_LIST_HEAD(&search_contexts);

            list_add(&dsc->others, &search_contexts);
            yaffsfs_SetDirRewound(dsc);
        }
    }
    return dir;
}

yaffs_DIR *yaffs_opendir_reldir(struct yaffs_obj *reldir, const YCHAR *dirname)
{
    yaffs_DIR *ret;

    yaffsfs_Lock();
    ret = yaffsfs_opendir_reldir_no_lock(reldir, dirname);
    yaffsfs_Unlock();
    return ret;
}

yaffs_DIR *yaffs_opendir_reldev(struct yaffs_dev *dev, const YCHAR *dirname)
{
    return yaffs_opendir_reldir(ROOT_DIR(dev), dirname);
}

yaffs_DIR *yaffs_opendir(const YCHAR *dirname)
{
    return yaffs_opendir_reldir(NULL, dirname);
}

struct yaffs_dirent *yaffsfs_readdir_no_lock(yaffs_DIR *dirp)
{
    struct yaffsfs_DirSearchContext *dsc;
    struct yaffs_dirent *retVal = NULL;

    dsc = (struct yaffsfs_DirSearchContext *) dirp;


    if (dsc && dsc->inUse)
    {
        yaffsfs_SetError(0);
        if (dsc->nextReturn)
        {
            dsc->de.d_ino =
                yaffs_get_equivalent_obj(dsc->nextReturn)->obj_id;
            dsc->de.d_dont_use = 0;
            dsc->de.d_off = dsc->offset++;
            yaffs_get_obj_name(dsc->nextReturn,
                               dsc->de.d_name, NAME_MAX);
            if (yaffs_strnlen(dsc->de.d_name, NAME_MAX + 1) == 0)
            {
                /* this should not happen! */
                yaffs_strcpy(dsc->de.d_name, _Y("zz"));
            }
            dsc->de.d_reclen = sizeof(struct yaffs_dirent);
            retVal = &dsc->de;
            yaffsfs_DirAdvance(dsc);
        }
        else
            retVal = NULL;
    }
    else
        yaffsfs_SetError(-EBADF);

    return retVal;

}
struct yaffs_dirent *yaffs_readdir(yaffs_DIR *dirp)
{
    struct yaffs_dirent *ret;

    yaffsfs_Lock();
    ret = yaffsfs_readdir_no_lock(dirp);
    yaffsfs_Unlock();
    return ret;
}

static void yaffsfs_rewinddir_no_lock(yaffs_DIR *dirp)
{
    struct yaffsfs_DirSearchContext *dsc;

    dsc = (struct yaffsfs_DirSearchContext *) dirp;

    if (yaffsfs_CheckMemRegion(dirp, sizeof(*dsc), 0) < 0)
        return;

    yaffsfs_SetDirRewound(dsc);

}

void yaffs_rewinddir(yaffs_DIR *dirp)
{
    yaffsfs_Lock();
    yaffsfs_rewinddir_no_lock(dirp);
    yaffsfs_Unlock();
}

struct yaffs_dirent *yaffs_readdir_fd(int fd)
{
    struct yaffs_dirent *ret = NULL;
    struct yaffsfs_FileDes *f;

    yaffsfs_Lock();
    f = yaffsfs_HandleToFileDes(fd);
    if (f && f->isDir && f->v.dir)
        ret = yaffsfs_readdir_no_lock(f->v.dir);
    yaffsfs_Unlock();
    return ret;
}

void yaffs_rewinddir_fd(int fd)
{
    struct yaffsfs_FileDes *f;

    yaffsfs_Lock();
    f = yaffsfs_HandleToFileDes(fd);
    if (f && f->isDir)
        yaffsfs_rewinddir_no_lock(f->v.dir);
    yaffsfs_Unlock();
}


static int yaffsfs_closedir_no_lock(yaffs_DIR *dirp)
{
    struct yaffsfs_DirSearchContext *dsc;

    dsc = (struct yaffsfs_DirSearchContext *) dirp;

    if (yaffsfs_CheckMemRegion(dirp, sizeof(*dsc), 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    dsc->inUse = 0;
    list_del(&dsc->others); /* unhook from list */

    return 0;
}

int yaffs_closedir(yaffs_DIR *dirp)
{
    int ret;

    yaffsfs_Lock();
    ret = yaffsfs_closedir_no_lock(dirp);
    yaffsfs_Unlock();
    return ret;
}

/* End of directory stuff */

int yaffs_symlink_reldir(struct yaffs_obj *reldir,
                         const YCHAR *oldpath, const YCHAR *newpath)
{
    struct yaffs_obj *parent = NULL;
    struct yaffs_obj *obj;
    YCHAR *name;
    int retVal = -1;
    int mode = 0;       /* ignore for now */
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(oldpath, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(newpath, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(newpath) < 0 || yaffsfs_CheckPath(oldpath) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();
    parent = yaffsfs_FindDirectory(reldir, newpath, &name, 0, &notDir, &loop);
    if (!parent && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!parent || yaffs_strnlen(name, 5) < 1)
        yaffsfs_SetError(-ENOENT);
    else if (yaffsfs_TooManyObjects(parent->my_dev))
        yaffsfs_SetError(-ENFILE);
    else if (parent->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (parent)
    {
        obj = yaffs_create_symlink(parent, name, mode, 0, 0, oldpath);
        if (obj)
            retVal = 0;
        else if (yaffsfs_FindObject(reldir, newpath, 0, 0,
                                    NULL, NULL, NULL))
            yaffsfs_SetError(-EEXIST);
        else
            yaffsfs_SetError(-ENOSPC);
    }

    yaffsfs_Unlock();

    return retVal;

}
int yaffs_symlink(const YCHAR *oldpath, const YCHAR *newpath)
{
    return yaffs_symlink_reldir(NULL, oldpath, newpath);
}

int yaffs_readlink_reldir(struct yaffs_obj *reldir, const YCHAR *path,
                          YCHAR *buf, int bufsiz)
{
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *dir = NULL;
    int retVal = -1;
    int notDir = 0;
    int loop = 0;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(buf, bufsiz, 1) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, path, 0, 1, &dir, &notDir, &loop);

    if (!dir && notDir)
        yaffsfs_SetError(-ENOTDIR);
    else if (loop)
        yaffsfs_SetError(-ELOOP);
    else if (!dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->variant_type != YAFFS_OBJECT_TYPE_SYMLINK)
        yaffsfs_SetError(-EINVAL);
    else
    {
        YCHAR *alias = obj->variant.symlink_variant.alias;
        memset(buf, 0, bufsiz);
        yaffs_strncpy(buf, alias, bufsiz - 1);
        retVal = 0;
    }
    yaffsfs_Unlock();
    return retVal;
}
int yaffs_readlink(const YCHAR *path, YCHAR *buf, int bufsiz)
{
    return yaffs_readlink_reldir(NULL, path, buf, bufsiz);
}

int yaffs_link_reldir(struct yaffs_obj *reldir,
                      const YCHAR *oldpath, const YCHAR *linkpath)
{
    /* Creates a link called newpath to existing oldpath */
    struct yaffs_obj *obj = NULL;
    struct yaffs_obj *lnk = NULL;
    struct yaffs_obj *obj_dir = NULL;
    struct yaffs_obj *lnk_dir = NULL;
    int retVal = -1;
    int notDirObj = 0;
    int notDirLnk = 0;
    int objLoop = 0;
    int lnkLoop = 0;
    YCHAR *newname;

    if (yaffsfs_CheckMemRegion(oldpath, 0, 0) < 0 ||
            yaffsfs_CheckMemRegion(linkpath, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(linkpath) < 0 || yaffsfs_CheckPath(oldpath) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    yaffsfs_Lock();

    obj = yaffsfs_FindObject(reldir, oldpath, 0, 1,
                             &obj_dir, &notDirObj, &objLoop);
    lnk = yaffsfs_FindObject(reldir, linkpath, 0, 0, NULL, NULL, NULL);
    lnk_dir = yaffsfs_FindDirectory(reldir, linkpath, &newname,
                                    0, &notDirLnk, &lnkLoop);

    if ((!obj_dir && notDirObj) || (!lnk_dir && notDirLnk))
        yaffsfs_SetError(-ENOTDIR);
    else if (objLoop || lnkLoop)
        yaffsfs_SetError(-ELOOP);
    else if (!obj_dir || !lnk_dir || !obj)
        yaffsfs_SetError(-ENOENT);
    else if (obj->my_dev->read_only)
        yaffsfs_SetError(-EROFS);
    else if (yaffsfs_TooManyObjects(obj->my_dev))
        yaffsfs_SetError(-ENFILE);
    else if (lnk)
        yaffsfs_SetError(-EEXIST);
    else if (lnk_dir->my_dev != obj->my_dev)
        yaffsfs_SetError(-EXDEV);
    else
    {
        retVal = yaffsfs_CheckNameLength(newname);

        if (retVal == 0)
        {
            lnk = yaffs_link_obj(lnk_dir, newname, obj);
            if (lnk)
                retVal = 0;
            else
            {
                yaffsfs_SetError(-ENOSPC);
                retVal = -1;
            }
        }
    }
    yaffsfs_Unlock();

    return retVal;
}
int yaffs_link(const YCHAR *oldpath, const YCHAR *linkpath)
{
    return yaffs_link_reldir(NULL, oldpath, linkpath);
}

int yaffs_mknod_reldir(struct yaffs_obj *reldir, const YCHAR *pathname,
                       mode_t mode, dev_t dev_val)
{
    (void) pathname;
    (void) mode;
    (void) dev_val;
    (void) reldir;

    yaffsfs_SetError(-EINVAL);
    return -1;
}

int yaffs_mknod_reldev(struct yaffs_dev *dev, const YCHAR *pathname, mode_t mode, dev_t dev_val)
{
    return yaffs_mknod_reldir(ROOT_DIR(dev), pathname, mode, dev_val);
}

int yaffs_mknod(const YCHAR *pathname, mode_t mode, dev_t dev_val)
{
    return yaffs_mknod_reldir(NULL, pathname, mode, dev_val);
}

/*
 * D E B U G   F U N C T I O N S
 */

/*
 * yaffs_n_handles()
 * Returns number of handles attached to the object
 */
int yaffs_n_handles_reldir(struct yaffs_obj *reldir, const YCHAR *path)
{
    struct yaffs_obj *obj;

    if (yaffsfs_CheckMemRegion(path, 0, 0) < 0)
    {
        yaffsfs_SetError(-EFAULT);
        return -1;
    }

    if (yaffsfs_CheckPath(path) < 0)
    {
        yaffsfs_SetError(-ENAMETOOLONG);
        return -1;
    }

    obj = yaffsfs_FindObject(reldir, path, 0, 1, NULL, NULL, NULL);

    if (obj)
        return yaffsfs_CountHandles(obj);
    else
        return -1;
}

int yaffs_n_handles(const YCHAR *path)
{
    return yaffs_n_handles_reldir(NULL, path);
}

int yaffs_get_error(void)
{
    return yaffsfs_GetLastError();
}

int yaffs_set_error(int error)
{
    yaffsfs_SetError(error);
    return 0;
}

struct yaffs_obj *yaffs_get_obj_from_fd(int handle)
{
    return yaffsfs_HandleToObject(handle);
}

int yaffs_dump_dev_reldir(struct yaffs_obj *reldir, const YCHAR *path)
{
#if 1
    (void) path;
    (void) reldir;
#else
    YCHAR *rest;


    if (!reldir)
        reldir = yaffsfs_FindRoot(path, &rest);

    if (reldir)
    {
        struct yaffs_dev *dev = reldir->my_dev;

        printf("\n"
               "n_page_writes.......... %d\n"
               "n_page_reads........... %d\n"
               "n_erasures....... %d\n"
               "n_gc_copies............ %d\n"
               "garbageCollections... %d\n"
               "passiveGarbageColl'ns %d\n"
               "\n",
               dev->n_page_writes,
               dev->n_page_reads,
               dev->n_erasures,
               dev->n_gc_copies,
               dev->garbageCollections, dev->passiveGarbageCollections);

    }
#endif
    return 0;
}

int yaffs_dump_dev(const YCHAR *path)
{
    return yaffs_dump_dev_reldir(NULL, path);
}
