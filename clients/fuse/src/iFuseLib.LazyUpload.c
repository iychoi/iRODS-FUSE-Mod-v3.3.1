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
#include "iFuseLib.LazyUpload.h"
#include "iFuseLib.Lock.h"
#include "iFuseLib.FSUtils.h"
#include "putUtil.h"

/**************************************************************************
 * global variables
 **************************************************************************/
static lazyUploadConfig_t LazyUploadConfig;
static rodsEnv *LazyUploadRodsEnv;
static rodsArguments_t *LazyUploadRodsArgs;

static Hashtable *LazyUploadBufferedFileTable_Created;
static Hashtable *LazyUploadBufferedFileTable_Opened;

/**************************************************************************
 * function definitions
 **************************************************************************/
static int _prepareBufferDir(const char *path);
static int _getBufferPath(const char *path, char *bufferPath);
static int _removeAllBufferedFiles();
static int _getiRODSPath(const char *path, char *iRODSPath);
static int _isLazyUploadFile(const char *path);
static int _commitLocalBuffer(const char *path, struct fuse_file_info *fi, lazyUploadFileInfo_t *lazyUploadFileInfo);
static int _upload(const char *path);


/**************************************************************************
 * public functions
 **************************************************************************/
int
initLazyUpload (lazyUploadConfig_t *lazyUploadConfig, rodsEnv *myLazyUploadRodsEnv, rodsArguments_t *myLazyUploadRodsArgs) {
    rodsLog (LOG_DEBUG, "initLazyUpload: MyLazyUploadConfig.lazyUpload = %d", lazyUploadConfig->lazyUpload);
    rodsLog (LOG_DEBUG, "initLazyUpload: MyLazyUploadConfig.bufferPath = %s", lazyUploadConfig->bufferPath);
    rodsLog (LOG_DEBUG, "initLazyUpload: empty space = %lld", getEmptypSpace(lazyUploadConfig->bufferPath));

    // copy given configuration
    memcpy(&LazyUploadConfig, lazyUploadConfig, sizeof(lazyUploadConfig_t));
    LazyUploadRodsEnv = myLazyUploadRodsEnv;
    LazyUploadRodsArgs = myLazyUploadRodsArgs;

    // init hashtables
    LazyUploadBufferedFileTable_Created = newHashTable(NUM_LAZYUPLOAD_FILE_HASH_SLOT);
    LazyUploadBufferedFileTable_Opened = newHashTable(NUM_LAZYUPLOAD_FILE_HASH_SLOT);

    // init lock
    INIT_LOCK(LazyUploadLock);

    _prepareBufferDir(lazyUploadConfig->bufferPath);

    // remove lazy-upload Buffered files
    _removeAllBufferedFiles();

    return (0);
}

int
uninitLazyUpload (lazyUploadConfig_t *lazyUploadConfig) {
    // remove incomplete lazy-upload caches
    _removeAllBufferedFiles();

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
isFileLazyUploading (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isFileLazyUploading: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    status = _isLazyUploadFile(iRODSPath);

    UNLOCK(LazyUploadLock);

    return status;
}

int
mknodLazyUploadBufferedFile(const char *path) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "mknodLazyUploadBufferedFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "mknodLazyUploadBufferedFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    // make dir
    makeParentDirs(bufferPath);

    // create an empty file
    rodsLog (LOG_DEBUG, "mknodLazyUploadBufferedFile: create a new Buffered file - %s", iRODSPath);
    int desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);
    close (desc);

    // add to hash table
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)malloc(sizeof(lazyUploadFileInfo_t));
    lazyUploadFileInfo->path = strdup(iRODSPath);
    lazyUploadFileInfo->accmode = 0; // clear
    lazyUploadFileInfo->localHandle = -1; // clear
    lazyUploadFileInfo->commitCount = 0; // clear
    lazyUploadFileInfo->curLocalOffsetStart = 0; // clear
    lazyUploadFileInfo->curOffset = 0; // clear
    INIT_STRUCT_LOCK((*lazyUploadFileInfo));

    insertIntoHashTable(LazyUploadBufferedFileTable_Created, iRODSPath, lazyUploadFileInfo);

    UNLOCK(LazyUploadLock);

    return status;
}

int
openLazyUploadBufferedFile(const char *path, int accmode) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    int desc;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    desc = -1;

    // check the given file is Buffered
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file opened
        rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: same file is already opened for lazy-upload - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EMFILE;
    }

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Created, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileInfo->localHandle > 0) {
            rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: same file is already opened for lazy-upload - %s", iRODSPath);
            UNLOCK(LazyUploadLock);
            return -EMFILE;
        } else {
            // update file handle and access mode
            desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);

            if(desc > 0) {
                lazyUploadFileInfo->accmode = accmode;
                lazyUploadFileInfo->localHandle = desc;
                rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: opens a file handle - %s", iRODSPath);

                // move to opened hashtable
                deleteFromHashTable(LazyUploadBufferedFileTable_Created, iRODSPath);
                insertIntoHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath, lazyUploadFileInfo);
            }
        }
    } else {
        rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: mknod is not called before opening - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EBADF;
    }

    UNLOCK(LazyUploadLock);

    return desc;
}

int
writeLazyUploadBufferedFile (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    off_t seek_status;
    int descInx;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    // check the given file is Buffered
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileInfo->localHandle < 0) {
            rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: wrong file descriptor - %s, %d", iRODSPath, lazyUploadFileInfo->localHandle);
            UNLOCK(LazyUploadLock);
            return -EBADF;
        }
    } else {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: file is not opened - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EBADF;
    }

    if(offset - lazyUploadFileInfo->curLocalOffsetStart < 0) {
        // backward?
        // commit local buffered data
        status = _commitLocalBuffer(path, fi, lazyUploadFileInfo);
        if(status < 0) {
            rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to commit local buffered data - %s", iRODSPath);
            UNLOCK(LazyUploadLock);
            return status; 
        }

        // rewrite directly to iRODS
        descInx = GET_IFUSE_DESC_INDEX(fi);

        status = _ifuseWrite (&IFuseDesc[descInx], (char *)buf, size, offset);
        unlockDesc (descInx);
        return status;
    }

    // try to write to local buffer
    seek_status = lseek (lazyUploadFileInfo->localHandle, offset - lazyUploadFileInfo->curLocalOffsetStart, SEEK_SET);
    if (seek_status != (offset - lazyUploadFileInfo->curLocalOffsetStart)) {
        status = (int)seek_status;
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to seek file desc - %d, %ld -> %ld", lazyUploadFileInfo->localHandle, offset - lazyUploadFileInfo->curLocalOffsetStart, seek_status);
        UNLOCK(LazyUploadLock);
        return status;
    }

    // TEST
    if (lazyUploadFileInfo->curOffset > 10*1024*1024) {
        status = -1;
        errno = ENOSPC;
    } else {
        status = write (lazyUploadFileInfo->localHandle, buf, size);
    }
    // TEST END

    //status = write (lazyUploadFileInfo->localHandle, buf, size);
    rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: write to opened lazy-upload Buffered file - %d", lazyUploadFileInfo->localHandle);

    if (status < 0) {
        // no space?
        if (errno == ENOSPC) {
            // handle no space
            // mode switch
            status = _commitLocalBuffer(path, fi, lazyUploadFileInfo);
            if(status < 0) {
                rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to change to appending mode - %s", iRODSPath);
                UNLOCK(LazyUploadLock);
                return status; 
            }

            // write to local buffer again
            seek_status = lseek (lazyUploadFileInfo->localHandle, offset - lazyUploadFileInfo->curLocalOffsetStart, SEEK_SET);
            if (seek_status != (offset - lazyUploadFileInfo->curLocalOffsetStart)) {
                status = (int)seek_status;
                rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to seek file desc - %d, %ld -> %ld", lazyUploadFileInfo->localHandle, offset - lazyUploadFileInfo->curLocalOffsetStart, seek_status);
                UNLOCK(LazyUploadLock);
                return status;
            }

            status = write (lazyUploadFileInfo->localHandle, buf, size);
            rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: write to opened lazy-upload Buffered file - %d", lazyUploadFileInfo->localHandle);

            if(status >= 0) {
                lazyUploadFileInfo->curOffset += status;
            }
        }
    } else {
        lazyUploadFileInfo->curOffset += status;
    }

    UNLOCK(LazyUploadLock);

    return status;
}

int
syncLazyUploadBufferedFile (const char *path, struct fuse_file_info *fi) {
    int status;
    char iRODSPath[MAX_NAME_LEN];
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "syncLazyUploadBufferedFile: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy-upload file handle opened
        status = _commitLocalBuffer(path, fi, lazyUploadFileInfo);
        if(status < 0) {
            rodsLog (LOG_DEBUG, "syncLazyUploadBufferedFile: failed to commit local buffer - %s", iRODSPath);
            UNLOCK(LazyUploadLock);
            return status; 
        }
    }
    
    UNLOCK(LazyUploadLock);
    return status;
}

int
closeLazyUploadBufferedFile (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "closeLazyUploadBufferedFile: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy-upload file handle opened
        if(lazyUploadFileInfo->localHandle > 0) {
            close(lazyUploadFileInfo->localHandle);
            lazyUploadFileInfo->localHandle = -1;
            rodsLog (LOG_DEBUG, "closeLazyUploadBufferedFile: close lazy-upload Buffered file handle - %s", iRODSPath);
        }

	deleteFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);

        if(lazyUploadFileInfo->path != NULL) {
            free(lazyUploadFileInfo->path);
            lazyUploadFileInfo->path = NULL;
        }

        free(lazyUploadFileInfo);
    }
    
    UNLOCK(LazyUploadLock);
    rodsLog (LOG_DEBUG, "closeLazyUploadBufferedFile: closed lazy-upload cache handle - %s", iRODSPath);
    return status;
}

/**************************************************************************
 * private functions
 **************************************************************************/
static int
_commitLocalBuffer(const char *path, struct fuse_file_info *fi, lazyUploadFileInfo_t *lazyUploadFileInfo) {
    int status;
    int desc;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    int descInx;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_commitLocalBuffer: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(path, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: failed to get Buffered lazy upload file path - %s", path);
        return status;
    }

    // close local buffer file handle
    if(lazyUploadFileInfo->localHandle < 0) {
        rodsLog (LOG_DEBUG, "_commitLocalBuffer: wrong file descriptor - %s, %d", iRODSPath, lazyUploadFileInfo->localHandle);
        return -EBADF;
    }

    // no write but requested flush
    if(lazyUploadFileInfo->curOffset == 0) {
        return 0;        
    }

    fsync(lazyUploadFileInfo->localHandle);
    close(lazyUploadFileInfo->localHandle);
    lazyUploadFileInfo->localHandle = -1;

    rodsLog (LOG_DEBUG, "_commitLocalBuffer: upload - %s", iRODSPath);
    
    if(lazyUploadFileInfo->commitCount == 0) {
        // put and reopen iRODS file
        dataObjInp_t dataObjInp;
        int fd;
        pathCache_t *tmpPathCache = NULL;
        iFuseConn_t *iFuseConn = NULL;
        iFuseDesc_t *desc = NULL;
        struct stat stbuf;
        char objPath[MAX_NAME_LEN];

        descInx = GET_IFUSE_DESC_INDEX(fi);

        rodsLog (LOG_DEBUG, "_commitLocalBuffer: closing existing iRODS file handle - %s - %d", iRODSPath, descInx);

        status = ifuseClose (&IFuseDesc[descInx]);
        clearPathFromCache((char *)path);
        if (status < 0) {
            int myError;
            if ((myError = getErrno (status)) > 0) {
                return (-myError);
            } else {
                return -ENOENT;
            }
        }

        // put
        rodsLog (LOG_DEBUG, "_commitLocalBuffer: uploading buffered file - %s", iRODSPath);

        status = _upload(iRODSPath);
        if(status != 0) {
            rodsLog (LOG_DEBUG, "_commitLocalBuffer: upload error - %s, %d", iRODSPath, status);
            return status;
        }

        // reopen file
        rodsLog (LOG_DEBUG, "_commitLocalBuffer: reopening iRODS file handle - %s", iRODSPath);

        memset (&dataObjInp, 0, sizeof (dataObjInp));
        //dataObjInp.openFlags = lazyUploadFileInfo->accmode;
        dataObjInp.openFlags = O_RDWR | O_APPEND;

        status = parseRodsPathStr ((char *) (path + 1) , LazyUploadRodsEnv, objPath);
        rstrcpy(dataObjInp.objPath, objPath, MAX_NAME_LEN);
        if (status < 0) {
            rodsLogError (LOG_ERROR, status, "_commitLocalBuffer: parseRodsPathStr of %s error", path);
            /* use ENOTDIR for this type of error */
            return -ENOTDIR;
        }

        iFuseConn = getAndUseConnByPath((char *) path, LazyUploadRodsEnv, &status);
        /* status = getAndUseIFuseConn(&iFuseConn, &MyRodsEnv); */
        if(status < 0) {
            rodsLogError (LOG_ERROR, status, "_commitLocalBuffer: cannot get connection for %s error", path);
            /* use ENOTDIR for this type of error */
            return -ENOTDIR;
        }

        status = _irodsGetattr (iFuseConn, path, &stbuf);

        fd = rcDataObjOpen (iFuseConn->conn, &dataObjInp);
        unuseIFuseConn (iFuseConn);

        if (fd < 0) {
            rodsLogError (LOG_ERROR, status, "_commitLocalBuffer: rcDataObjOpen of %s error, status = %d", path, fd);
            return -ENOENT;
        }

        fileCache_t *fileCache = addFileCache(fd, objPath, (char *) path, NULL, stbuf.st_mode, stbuf.st_size, NO_FILE_CACHE);
        matchAndLockPathCache((char *) path, &tmpPathCache);
        if(tmpPathCache == NULL) {
            pathExist((char *) path, fileCache, &stbuf, NULL);
        } else {
            _addFileCacheForPath(tmpPathCache, fileCache);
            UNLOCK_STRUCT(*tmpPathCache);
        }

        desc = newIFuseDesc (objPath, (char *) path, fileCache, &status);
        if (desc == NULL) {
            rodsLogError (LOG_ERROR, status, "_commitLocalBuffer: allocIFuseDesc of %s error", path);
            return -ENOENT;
        }

        SET_IFUSE_DESC_INDEX(fi, desc->index);
        rodsLog (LOG_DEBUG, "_commitLocalBuffer: created new file handle - %s - %d", iRODSPath, desc->index);

    } else {
        // append
        int localDesc = open (bufferPath, O_RDONLY, 0755);
        char *buffer = (char*)malloc(FUSE_LAZY_UPLOAD_MEM_BUFFER_SIZE);
        int grandTotalReadLen = 0;

        if(localDesc < 0) {
            rodsLog (LOG_DEBUG, "_commitLocalBuffer: file descriptor error - %s, %d", iRODSPath, localDesc);
            return -ENOENT;
        }

        descInx = GET_IFUSE_DESC_INDEX(fi);

        if (checkFuseDesc (descInx) < 0) {
            return -EBADF;
        }

        status = 0;
        while(grandTotalReadLen < lazyUploadFileInfo->curOffset) {
            int totalReadLen = 0;
            int totalWriteLen = 0;

            while(totalReadLen < FUSE_LAZY_UPLOAD_MEM_BUFFER_SIZE) {
                int readLen = read(localDesc, buffer + totalReadLen, FUSE_LAZY_UPLOAD_MEM_BUFFER_SIZE - totalReadLen);
                if(readLen > 0) {
                    totalReadLen += readLen;
                } else if(readLen == 0) {
                    // EOF
                    break;
                } else if(readLen < 0) {
                    rodsLog (LOG_DEBUG, "_commitLocalBuffer: buffered file read error - %s, %d", iRODSPath, localDesc);
                    status = getErrno (readLen);
                    break;
                }
            }

            if(status >= 0) {
                while(totalWriteLen < totalReadLen) {
                    int writeLen = _ifuseWrite (&IFuseDesc[descInx], buffer + totalWriteLen, totalReadLen - totalWriteLen, lazyUploadFileInfo->curLocalOffsetStart + totalWriteLen);
                    if(writeLen > 0) {
                        totalWriteLen += writeLen;
                    } else if(writeLen == 0) {
                        // EOF
                        break;
                    } else if(writeLen < 0) {
                        rodsLog (LOG_DEBUG, "_commitLocalBuffer: iRODS file write error - %s", iRODSPath);
                        status = getErrno (writeLen);
                        break;
                    }
                }
            }

            grandTotalReadLen += totalReadLen;
        }

        unlockDesc (descInx);

        free(buffer);
        close(localDesc);
    }

    rodsLog (LOG_DEBUG, "_commitLocalBuffer: reset local buffered file - %s", iRODSPath);

    // reset local buffer file
    desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);
    lazyUploadFileInfo->localHandle = desc;
    lazyUploadFileInfo->commitCount++;
    lazyUploadFileInfo->curLocalOffsetStart += lazyUploadFileInfo->curOffset;
    lazyUploadFileInfo->curOffset = 0;
    return (0);
}

static int
_upload (const char *path) {
    int status;
    rcComm_t *conn;
    rodsPathInp_t rodsPathInp;
    rErrMsg_t errMsg;
    char bufferPath[MAX_NAME_LEN];
    char destiRODSDir[MAX_NAME_LEN];

    status = getParentDir(path, destiRODSDir);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: failed to get parent dir - %s", path);
        return status;
    }

    status = _getBufferPath(path, bufferPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: failed to get Buffered lazy upload file path - %s", path);
        return status;
    }

    // set src path
    memset( &rodsPathInp, 0, sizeof( rodsPathInp_t ) );
    addSrcInPath( &rodsPathInp, bufferPath );
    status = parseLocalPath (&rodsPathInp.srcPath[0]);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: parseLocalPath error : %d", status);
        return status;
    }

    // set dest path
    rodsPathInp.destPath = ( rodsPath_t* )malloc( sizeof( rodsPath_t ) );
    memset( rodsPathInp.destPath, 0, sizeof( rodsPath_t ) );
    rstrcpy( rodsPathInp.destPath->inPath, destiRODSDir, MAX_NAME_LEN );
    status = parseRodsPath (rodsPathInp.destPath, LazyUploadRodsEnv);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: parseRodsPath error : %d", status);
        return status;
    }

    // Connect
    conn = rcConnect (LazyUploadRodsEnv->rodsHost, LazyUploadRodsEnv->rodsPort, LazyUploadRodsEnv->rodsUserName, LazyUploadRodsEnv->rodsZone, RECONN_TIMEOUT, &errMsg);
    if (conn == NULL) {
        rodsLog (LOG_DEBUG, "_upload: error occurred while connecting to irods");
        return -EPIPE;
    }

    // Login
    if (strcmp (LazyUploadRodsEnv->rodsUserName, PUBLIC_USER_NAME) != 0) { 
        status = clientLogin(conn);
        if (status != 0) {
            rodsLog (LOG_DEBUG, "_upload: ClientLogin error : %d", status);
            rcDisconnect(conn);
            return status;
        }
    }

    // upload Buffered file
    rodsLog (LOG_DEBUG, "_upload: upload %s", bufferPath);
    bool prev = LazyUploadRodsArgs->force;
    LazyUploadRodsArgs->force = True;
    status = putUtil (&conn, LazyUploadRodsEnv, LazyUploadRodsArgs, &rodsPathInp);
    LazyUploadRodsArgs->force = prev;
    rodsLog (LOG_DEBUG, "_upload: complete uploading %s -> %s", bufferPath, destiRODSDir);

    // Disconnect 
    rcDisconnect(conn);

    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: putUtil error : %d", status);
        return status;
    }

    return 0;
}

static int
_isLazyUploadFile(const char *path) {
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;

    if (path == NULL) {
        rodsLog (LOG_DEBUG, "_isLazyUploadFile: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, (char *) path);
    if (lazyUploadFileInfo == NULL) {
        return -1;
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
_prepareBufferDir(const char *path) {
    int status;

    status = makeDirs(path);

    return status; 
}

static int
_removeAllBufferedFiles() {
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
