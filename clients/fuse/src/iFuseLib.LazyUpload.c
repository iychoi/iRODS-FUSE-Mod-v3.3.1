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

static concurrentList_t *LazyUploadThreadList;
static Hashtable *LazyUploadThreadTable;

static Hashtable *LazyUploadBufferedFileTable_Created;
static Hashtable *LazyUploadBufferedFileTable_Opened;

/**************************************************************************
 * function definitions
 **************************************************************************/
static void *_lazyUploadThread(void *arg);
static int _upload(const char *path);
static int _prepareBufferDir(const char *path);
static int _getBufferPath(const char *path, char *bufferPath);
static int _hasBufferedFile(const char *path);
static int _removeAllBufferedFiles();
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
    LazyUploadThreadTable = newHashTable(NUM_LAZYUPLOAD_THREAD_HASH_SLOT);
    LazyUploadBufferedFileTable_Created = newHashTable(NUM_LAZYUPLOAD_FILE_HASH_SLOT);
    LazyUploadBufferedFileTable_Opened = newHashTable(NUM_LAZYUPLOAD_FILE_HASH_SLOT);

    // init lists
    LazyUploadThreadList = newConcurrentList();

    // init lock
    INIT_LOCK(LazyUploadLock);

    _prepareBufferDir(lazyUploadConfig->bufferPath);

    // remove lazy-upload Buffered files
    _removeAllBufferedFiles();

    return (0);
}

int
waitLazyUploadJobs () {
    int status;
    int i;
    int size;

    // destroy lazy-upload thread list
    size = listSize(LazyUploadThreadList);
    for(i=0;i<size;i++) {
        LOCK(LazyUploadLock);
        lazyUploadThreadInfo_t *threadInfo = (lazyUploadThreadInfo_t *)removeFirstElementOfConcurrentList(LazyUploadThreadList);
        if(threadInfo != NULL) {
            rodsLog (LOG_DEBUG, "uninitLazyUpload: Waiting for a lazy-upload job - %s", threadInfo->path);
            UNLOCK(LazyUploadLock);
#ifdef USE_BOOST
            status = threadInfo->thread->join();
#else
            status = pthread_join(threadInfo->thread, NULL);
#endif
        } else {
            UNLOCK(LazyUploadLock);
        }
    }
    return 0;
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
isFileBufferedForLazyUpload (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isFileBufferedForLazyUpload: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    status = _hasBufferedFile(iRODSPath);

    UNLOCK(LazyUploadLock);

    return status;
}

int
isBufferedFileUploading(const char *path) {
    int status;
    lazyUploadThreadInfo_t *threadInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "isBufferedFileUploading: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(LazyUploadLock);

    // check the given file is lazy uploading
    threadInfo = (lazyUploadThreadInfo_t *)lookupFromHashTable(LazyUploadThreadTable, iRODSPath);
    if(threadInfo != NULL) {
        UNLOCK(LazyUploadLock);
        return 0;
    }

    UNLOCK(LazyUploadLock);
    return -1;
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
    lazyUploadFileInfo->handle = -1; // clear
    lazyUploadFileInfo->accmode = 0; // clear
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
        if(lazyUploadFileInfo->handle > 0) {
            rodsLog (LOG_DEBUG, "openLazyUploadBufferedFile: same file is already opened for lazy-upload - %s", iRODSPath);
            UNLOCK(LazyUploadLock);
            return -EMFILE;
        } else {
            // update file handle and access mode
            desc = open (bufferPath, O_RDWR|O_CREAT|O_TRUNC, 0755);

            if(desc > 0) {
                lazyUploadFileInfo->handle = desc;
                lazyUploadFileInfo->accmode = accmode;
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
writeLazyUploadBufferedFile (const char *path, const char *buf, size_t size, off_t offset) {
    int status;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    int desc;
    off_t seek_status;

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

    desc = -1;

    // check the given file is Buffered
    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
    if(lazyUploadFileInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileInfo->handle > 0) {
            desc = lazyUploadFileInfo->handle;
        } else {
            rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: wrong file descriptor - %s, %d", iRODSPath, lazyUploadFileInfo->handle);
            UNLOCK(LazyUploadLock);
            return -EBADF;
        }
    } else {
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: file is not opened - %s", iRODSPath);
        UNLOCK(LazyUploadLock);
        return -EBADF;
    }

    seek_status = lseek (desc, offset, SEEK_SET);
    if (seek_status != offset) {
        status = (int)seek_status;
        rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: failed to seek file desc - %d, %ld -> %ld", desc, offset, seek_status);

        UNLOCK(LazyUploadLock);
        return status;
    }

    status = write (desc, buf, size);
    rodsLog (LOG_DEBUG, "writeLazyUploadBufferedFile: write to opened lazy-upload Buffered file - %d", desc);

    UNLOCK(LazyUploadLock);

    return status;
}

int
closeLazyUploadBufferedFile (const char *path) {
    int status;
    lazyUploadThreadInfo_t *threadInfo = NULL;
    lazyUploadThreadData_t *threadData = NULL;
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
        if(lazyUploadFileInfo->handle > 0) {
            fsync(lazyUploadFileInfo->handle);
            close(lazyUploadFileInfo->handle);
            lazyUploadFileInfo->handle = -1;
            rodsLog (LOG_DEBUG, "closeLazyUploadBufferedFile: close lazy-upload Buffered file handle - %s", iRODSPath);
        }

        // create a new thread to upload
        threadInfo = (lazyUploadThreadInfo_t *)malloc(sizeof(lazyUploadThreadInfo_t));
        threadInfo->path = strdup(iRODSPath);
        threadInfo->running = LAZYUPLOAD_THREAD_RUNNING;
        INIT_STRUCT_LOCK((*threadInfo));

        deleteFromHashTable(LazyUploadBufferedFileTable_Opened, iRODSPath);
        deleteFromHashTable(LazyUploadBufferedFileTable_Created, iRODSPath);
        addToConcurrentList(LazyUploadThreadList, threadInfo);
        insertIntoHashTable(LazyUploadThreadTable, iRODSPath, threadInfo);

        // prepare thread argument
        threadData = (lazyUploadThreadData_t *)malloc(sizeof(lazyUploadThreadData_t));
        threadData->path = strdup(iRODSPath);
        threadData->threadInfo = threadInfo;

        rodsLog (LOG_DEBUG, "closeLazyUploadBufferedFile: start uploading - %s", iRODSPath);
#ifdef USE_BOOST
        status = threadInfo->thread = new boost::thread(_lazyUploadThread, (void *)threadData);
#else
        status = pthread_create(&threadInfo->thread, NULL, _lazyUploadThread, (void *)threadData);
#endif        

        if(lazyUploadFileInfo->path != NULL) {
            free(lazyUploadFileInfo->path);
            lazyUploadFileInfo->path = NULL;
        }
    }
    
    UNLOCK(LazyUploadLock);
    return status;
}

/**************************************************************************
 * private functions
 **************************************************************************/
static void *_lazyUploadThread(void *arg) {
    int status;
    lazyUploadThreadData_t *threadData = (lazyUploadThreadData_t *)arg;
    lazyUploadThreadInfo_t *threadInfo = NULL;

    if(threadData == NULL) {
        rodsLog (LOG_DEBUG, "_lazyUploadThread: given thread argument is null");
        pthread_exit(NULL);
    }

    threadInfo = threadData->threadInfo;

    rodsLog (LOG_DEBUG, "_lazyUploadThread: upload - %s", threadData->path);
    
    status = _upload(threadData->path);
    if(status != 0) {
        rodsLog (LOG_DEBUG, "_lazyUploadThread: upload error - %d", status);
    }

    // uploading is done
    LOCK(LazyUploadLock);

    // change thread status
    LOCK_STRUCT(*threadInfo);
    threadInfo->running = LAZYUPLOAD_THREAD_IDLE;
    UNLOCK_STRUCT(*threadInfo);

    // release threadData
    rodsLog (LOG_DEBUG, "_lazyUploadThread: thread finished - %s", threadData->path);
    if(threadData->path != NULL) {
        free(threadData->path);
        threadData->path = NULL;
    }

    free(threadData);

    // remove from hash table
    removeFromConcurrentList2(LazyUploadThreadList, threadInfo);
    deleteFromHashTable(LazyUploadThreadTable, threadInfo->path);

    if(threadInfo != NULL) {
        if(threadInfo->path != NULL) {
            free(threadInfo->path);
            threadInfo->path = NULL;
        }
        free(threadInfo);
    }

    UNLOCK(LazyUploadLock);
    pthread_exit(NULL);
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

    // move to preload
    LOCK(LazyUploadLock);
    status = moveToPreloadedDir(bufferPath, path);
    if(status < 0) {
        rodsLog (LOG_DEBUG, "_upload: moveToPreloadedDir error : %d", status);
        UNLOCK(LazyUploadLock);
        return status;
    }

    UNLOCK(LazyUploadLock);
    return 0;
}

static int
_hasBufferedFile(const char *path) {
    int status;
    char bufferPath[MAX_NAME_LEN];
    struct stat stbufCache;
    lazyUploadFileInfo_t *lazyUploadFileInfo = NULL;

    if (path == NULL) {
        rodsLog (LOG_DEBUG, "_hasBufferedFile: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = _getBufferPath(path, bufferPath)) < 0) {
        rodsLog (LOG_DEBUG, "_hasBufferedFile: _getBufferPath error : %d", status);
        return status;
    }

    lazyUploadFileInfo = (lazyUploadFileInfo_t *)lookupFromHashTable(LazyUploadBufferedFileTable_Opened, (char *) path);
    if (lazyUploadFileInfo == NULL) {
        return -1;
    }

    if ((status = stat(bufferPath, &stbufCache)) < 0) {
        //rodsLog (LOG_DEBUG, status, "_hasBufferedFile: stat error");
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
