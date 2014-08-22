#include <iostream>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "irodsFs.h"
#include "iFuseLib.h"
#include "iFuseOper.h"
#include "hashtable.h"
#include "miscUtil.h"
#include "iFuseLib.Lock.h"
#include "iFuseLib.FSUtils.h"
#include "putUtil.h"

/**************************************************************************
 * global variables
 **************************************************************************/
static lazyUploadConfig_t LazyUploadConfig;
static rodsEnv *LazyUploadRodsEnv;
static rodsArguments_t *LazyUploadRodsArgs;
static Hashtable *LazyUploadBufferredFileTable;
static Hashtable *LazyUploadingTable;

/**************************************************************************
 * function definitions
 **************************************************************************/
static int _uploadFile (const char *path);
static int _prepareLazyUploadBufferDir(const char *path);
static int _getBufferPath(const char *path, char *bufferPath);
static int _hasBufferredFile(const char *path);
static int _removeAllBufferredFiles();
static int _getiRODSPath(const char *path, char *iRODSPath);

/**************************************************************************
 * public functions
 **************************************************************************/
int
initLazyUpload (lazyUploadConfig_t *lazyUploadConfig, rodsEnv *myLazyUploadRodsEnv, rodsArguments_t *myLazyUploadRodsArgs) {
    rodsLog (LOG_DEBUG, "initLazyUpload: MyLazyUploadConfig.lazyUpload = %d", lazyUploadConfig->lazyUpload);
    rodsLog (LOG_DEBUG, "initLazyUpload: MyLazyUploadConfig.bufferPath = %s", lazyUploadConfig->bufferPath);

    // copy given configuration
    memcpy(&LazyUploadConfig, lazyUploadConfig, sizeof(lazyUploadConfig_t));
    LazyUploadRodsEnv = myLazyUploadRodsEnv;
    LazyUploadRodsArgs = myLazyUploadRodsArgs;

    // init hashtables
    LazyUploadBufferredFileTable = newHashTable(NUM_LAZYUPLOAD_FILE_HASH_SLOT);
    LazyUploadingTable = newHashTable(NUM_LAZYUPLOAD_UPLOADING_HASH_SLOT);

    // init lock
    INIT_LOCK(LazyUploadLock);

    _prepareLazyUploadBufferDir(lazyUploadConfig->bufferPath);

    // remove lazy-upload bufferred files
    _removeAllBufferredFiles();

    return (0);
}

int
uninitLazyUpload (lazyUploadConfig_t *lazyUploadConfig) {
    // remove incomplete lazy-upload caches
    _removeAllBufferredFiles();

    FREE_LOCK(LazyUploadLock);
    return (0);
}

int
isLazyUploadEnabled() {
    // check whether lazy-upload is enabled
    if(LazyUploadConfig.lazyUpload == 0) {
        return -1;
    }
    return 0;
}

int
isLazyUploadBufferred (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    status = _hasBufferredFile(iRODSPath);

    UNLOCK(LazyUploadLock);

    return status;
}

int
isBufferredFileUploading(const char *path) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isBufferredFileUploading: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isBufferredFileUploading: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    // check the given file is lazy uploading
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadingTable, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload bufferred file
        rodsLog (LOG_DEBUG, "isBufferredFileUploading: uploading thread is running - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return 0;
    }

    UNLOCK(LazyUploadLock);

    return -1;
}

int
mknodLazyUploadBufferredFile(const char *path) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "mknodLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "mknodLazyUploadBufferredFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    // make dir
    prepareDir(bufferPath);

    // create an empty file
    rodsLog (LOG_DEBUG, "mknodLazyUploadBufferredFile: create a new bufferred file - %s", iRODSPath);
    int desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);
    close (desc);

    // add to hash table
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)malloc(sizeof(lazyUploadFileInfo_t));
    lazyUploadFileInfo->path = strdup(iRODSPath);
    lazyUploadFileInfo->handle = -1; // clear
    lazyUploadFileInfo->accmode = 0; // clear
    INIT_STRUCT_LOCK((*lazyUploadFileInfo));

    insertIntoHashTable(LazyUploadBufferredFileTable, iRODSPath, lazyUploadFileInfo);

    UNLOCK(LazyUploadLock);

    return status;
}

int
openLazyUploadBufferredFile(const char *path, int accmode) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    int desc;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferredFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    desc = -1;

    // check the given file is bufferred
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferredFileTable, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileInfo->handle > 0) {
            rodsLog (LOG_DEBUG, "openLazyUploadBufferredFile: same file is already opened for lazy-upload - %s", iRODSPath);
            UNLOCK(LazyUploadLock);
            return -EMFILE;
        } else {
            // update file handle and access mode
            desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);

            if(desc > 0) {
                lazyUploadFileInfo->handle = desc;
                lazyUploadFileInfo->accmode = accmode;
                rodsLog (LOG_DEBUG, "openLazyUploadBufferredFile: opens a file handle - %s", iRODSPath);
            }
        }
    } else {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferredFile: mknod is not called before opening - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EBADF;
    }

    UNLOCK(LazyUploadLock);

    return desc;
}

int
writeLazyUploadBufferredFile (const char *path, const char *buf, size_t size, off_t offset) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    int desc;
    off_t seek_status;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    desc = -1;

    // check the given file is bufferred
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferredFileTable, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileInfo->handle > 0) {
            desc = lazyUploadFileInfo->handle;
        } else {
            rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: wrong file descriptor - %s, %d", iRODSPath, lazyUploadFileInfo->handle);
            UNLOCK(LazyUploadLock);
            return -EBADF;
        }
    } else {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: file is not opened - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EBADF;
    }

    seek_status = lseek (desc, offset, SEEK_SET);
    if (seek_status != offset) {
        status = (int)seek_status;
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: failed to seek file desc - %d, %ld -> %ld", desc, offset, seek_status);

        UNLOCK(LazyUploadLock);
        return status;
    }

    status = write (desc, buf, size);
    rodsLog (LOG_DEBUG, "writeLazyUploadBufferredFile: write to opened lazy-upload bufferred file - %d", desc);

    UNLOCK(LazyUploadLock);

    return status;
}

int
closeLazyUploadBufferredFile (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    lazyUploadFileInfo_t *tmpLazyUploadFileInfo = NULL;

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "closeLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferredFileTable, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy-upload file handle opened
        if(lazyUploadFileInfo->handle > 0) {
            fsync(lazyUploadFileInfo->handle);
            close(lazyUploadFileInfo->handle);
            lazyUploadFileInfo->handle = -1;
            rodsLog (LOG_DEBUG, "closeLazyUploadBufferredFile: close lazy-upload bufferred file handle - %s", iRODSPath);
        }

        // move to uploading table
        deleteFromHashTable(LazyUploadBufferredFileTable, iRODSPath);
        insertIntoHashTable(LazyUploadingTable, iRODSPath, lazyUploadFileInfo);

        // temporarily release
        UNLOCK(LazyUploadLock);
        // upload here
        _uploadFile(iRODSPath);
        LOCK(LazyUploadLock);

        if(lazyUploadFileInfo->path != NULL) {
            free(lazyUploadFileInfo->path);
            lazyUploadFileInfo->path = NULL;
        }
    
        // remove from hash table
        tmpLazyUploadFileInfo = (lazyUploadFileInfo_t *)deleteFromHashTable(LazyUploadingTable, iRODSPath);
        if(tmpLazyUploadFileInfo != NULL) {
            free(tmpLazyUploadFileInfo);
        }
    }
    
    UNLOCK(LazyUploadLock);

    return status;
}

/**************************************************************************
 * private functions
 **************************************************************************/
static int
_uploadFile (const char *path) {
    int status;
    rcComm_t *conn;
    rodsPathInp_t rodsPathInp;
    rErrMsg_t errMsg;
    char bufferPath[MAX_NAME_LEN];
    char destiRODSDir[MAX_NAME_LEN];

    status = getParentDir(path, destiRODSDir);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: failed to get parent dir - %s", path);
        return status;
    }

    status = _getBufferPath(path, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: failed to get bufferred lazy upload file path - %s", path);
        return status;
    }

    // set src path
    memset( &rodsPathInp, 0, sizeof( rodsPathInp_t ) );
    addSrcInPath( &rodsPathInp, bufferPath );
    status = parseLocalPath (&rodsPathInp.srcPath[0]);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: parseLocalPath error : %d", status);
        return status;
    }

    // set dest path
    rodsPathInp.destPath = ( rodsPath_t* )malloc( sizeof( rodsPath_t ) );
    memset( rodsPathInp.destPath, 0, sizeof( rodsPath_t ) );
    rstrcpy( rodsPathInp.destPath->inPath, destiRODSDir, MAX_NAME_LEN );
    status = parseRodsPath (rodsPathInp.destPath, LazyUploadRodsEnv);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: parseRodsPath error : %d", status);
        return status;
    }

    // Connect
    conn = rcConnect (LazyUploadRodsEnv->rodsHost, LazyUploadRodsEnv->rodsPort, LazyUploadRodsEnv->rodsUserName, LazyUploadRodsEnv->rodsZone, RECONN_TIMEOUT, &errMsg);
    if (conn == NULL) {
        rodsLog (LOG_DEBUG, "uploadFile: error occurred while connecting to irods");
        return -EPIPE;
    }

    // Login
    if (strcmp (LazyUploadRodsEnv->rodsUserName, PUBLIC_USER_NAME) != 0) { 
        status = clientLogin(conn);
        if (status != 0) {
            rodsLog (LOG_DEBUG, "uploadFile: ClientLogin error : %d", status);
            rcDisconnect(conn);
            return status;
        }
    }

    // upload bufferred file
    rodsLog (LOG_DEBUG, "uploadFile: upload %s", bufferPath);
    bool prev = LazyUploadRodsArgs->force;
    LazyUploadRodsArgs->force = True;
    status = putUtil (&conn, LazyUploadRodsEnv, LazyUploadRodsArgs, &rodsPathInp);
    LazyUploadRodsArgs->force = prev;
    rodsLog (LOG_DEBUG, "uploadFile: complete uploading %s -> %s", bufferPath, destiRODSDir);

    // Disconnect 
    rcDisconnect(conn);

    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: putUtil error : %d", status);
        return status;
    }

    // move to preload
    status = moveToPreloadedDir(bufferPath, path);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "uploadFile: moveToPreloadedDir error : %d", status);
        return status;
    }

    return 0;
}

static int
_hasBufferredFile(const char *path) {
    int status;
    char bufferPath[MAX_NAME_LEN];
    struct stat stbufCache;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;

    if (path == NULL) {
        rodsLog (LOG_DEBUG, "_hasBufferredFile: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = _getBufferPath(path, bufferPath)) < 0) {
        rodsLog (LOG_DEBUG, "_hasBufferredFile: _getBufferPath error : %d", status);
        return status;
    }

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferredFileTable, (char *) path);
    if (lazyUploadFileInfo == NULL) {
        return -1;
    }

    if ((status = stat(bufferPath, &stbufCache)) < 0) {
        //rodsLog (LOG_DEBUG, status, "_hasBufferredFile: stat error");
        return status;
    }

    return (0);
}

static int
_getBufferPath(const char *path, char *bufferPath) {
    if (path == NULL || bufferPath == NULL) {
        rodsLog (LOG_DEBUG, "_getBufferPath: given path or bufferPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if(strlen(path) > 0 && path[0] == '/') {
        snprintf(bufferPath, MAX_NAME_LEN, "%s%s", LazyUploadConfig.bufferPath, path);
    } else {
        snprintf(bufferPath, MAX_NAME_LEN, "%s/%s", LazyUploadConfig.bufferPath, path);
    }
    return (0);
}

static int
_prepareLazyUploadBufferDir(const char *path) {
    int status;

    status = makeDirs(path);

    return status; 
}

static int
_removeAllBufferredFiles() {
    int status;
    
    if((status = emptyDir(LazyUploadConfig.bufferPath)) < 0) {
        return status;
    }

    return 0;
}

static int
_getiRODSPath(const char *path, char *iRODSPath) {
    return getiRODSPath(path, iRODSPath, LazyUploadRodsEnv->rodsHome, LazyUploadRodsEnv->rodsCwd);
}
