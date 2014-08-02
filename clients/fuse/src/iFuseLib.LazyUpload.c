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
static Hashtable *LazyUploadFileHandleTable;

/**************************************************************************
 * function definitions
 **************************************************************************/
static int _prepareLazyUploadBufferDir(const char *path);
static int _getBufferPath(const char *path, char *bufferPath);
static int _hasBufferredFile(const char *path);
static int _removeAllBufferredFiles();
static int _removeBufferredFile(const char *path);
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
    LazyUploadFileHandleTable = newHashTable(NUM_LAZYUPLOAD_FILEHANDLE_HASH_SLOT);

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
prepareLazyUploadBufferredFile(const char *path) {
    int status;
    lazyUploadFileHandleInfo_t *lazyUploadFileHandleInfo = NULL;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "prepareLazyUploadBufferredFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "prepareLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "prepareLazyUploadBufferredFile: _getBufferPath error.");
        rodsLog (LOG_ERROR, "prepareLazyUploadBufferredFile: failed to get buffer path - %s", iRODSPath);
        return status;
    }

    LOCK(LazyUploadLock);

    // check the given file is lazy uploading
    lazyUploadFileHandleInfo = (lazyUploadFileHandleInfo_t *)lookupFromHashTable(LazyUploadFileHandleTable, iRODSPath);
    if(lazyUploadFileHandleInfo != NULL) {
        // has lazy upload file handle opened
        if(lazyUploadFileHandleInfo->handle > 0) {
            close(lazyUploadFileHandleInfo->handle);
            lazyUploadFileHandleInfo->handle = -1;
            rodsLog (LOG_DEBUG, "prepareLazyUploadBufferredFile: close lazy upload cache handle - %s", iRODSPath);
        }

        if(lazyUploadFileHandleInfo->path != NULL) {
            free(lazyUploadFileHandleInfo->path);
        }

        // remove from hash table
        deleteFromHashTable(LazyUploadFileHandleTable, iRODSPath);

        // remove file
        status = unlink(bufferPath);
        if(status < 0) {
            rodsLog (LOG_ERROR, "prepareLazyUploadBufferredFile: unlink failed - %s", iRODSPath);
        }
    }

    // make dir
    prepareDir(bufferPath);

    // TODO : fix this
    /*
    // create an empty file
    rodsLog (LOG_DEBUG, "prepareLazyUploadBufferredFile: create a bufferred file - %s", iRODSPath);
    int desc = open (bufferPath, O_RDWR|O_CREAT, 0755);
    close (desc);

    // add lazy upload cache handle to hash table
    desc = open (preloadCachePath, O_RDWR);
    lseek (desc, offset, SEEK_SET);
    status = read (desc, buf, size);
    rodsLog (LOG_DEBUG, "readPreloadedFile: read from preloaded cache path - %s", iRODSPath);

    if(desc > 0) {
        lazyUploadFileHandleInfo = (lazyUploadFileHandleInfo_t *)malloc(sizeof(lazyUploadFileHandleInfo_t));
        lazyUploadFileHandleInfo->path = strdup(iRODSPath);
        lazyUploadFileHandleInfo->handle = desc;
        INIT_STRUCT_LOCK((*lazyUploadFileHandleInfo));

        insertIntoHashTable(LazyUploadFileHandleTable, iRODSPath, lazyUploadFileHandleInfo);
    }
    */
    UNLOCK(LazyUploadLock);

    return status;
}

int
uploadFile (const char *path) {
    int status;
    rcComm_t *conn;
    rodsPathInp_t rodsPathInp;
    rErrMsg_t errMsg;
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];
    char destiRODSDir[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "uploadFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = getParentDir(iRODSPath, destiRODSDir);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: getParentDir error.");
        rodsLog (LOG_ERROR, "uploadFile: failed to get parent dir - %s", iRODSPath);
        return status;
    }

    status = _getBufferPath(iRODSPath, bufferPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: _getBufferPath error.");
        rodsLog (LOG_ERROR, "uploadFile: failed to get bufferred lazy upload file path - %s", iRODSPath);
        return status;
    }

    // set src path
    memset( &rodsPathInp, 0, sizeof( rodsPathInp_t ) );
    addSrcInPath( &rodsPathInp, bufferPath );
    status = parseLocalPath (&rodsPathInp.srcPath[0]);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: parseLocalPath error.");
        return status;
    }

    // set dest path
    rodsPathInp.destPath = ( rodsPath_t* )malloc( sizeof( rodsPath_t ) );
    memset( rodsPathInp.destPath, 0, sizeof( rodsPath_t ) );
    rstrcpy( rodsPathInp.destPath->inPath, destiRODSDir, MAX_NAME_LEN );
    status = parseRodsPath (rodsPathInp.destPath, LazyUploadRodsEnv);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: parseRodsPath error.");
        return status;
    }

    // Connect
    conn = rcConnect (LazyUploadRodsEnv->rodsHost, LazyUploadRodsEnv->rodsPort, LazyUploadRodsEnv->rodsUserName, LazyUploadRodsEnv->rodsZone, RECONN_TIMEOUT, &errMsg);
    if (conn == NULL) {
        rodsLog (LOG_ERROR, "uploadFile: error occurred while connecting to irods");
        return -1;
    }

    // Login
    if (strcmp (LazyUploadRodsEnv->rodsUserName, PUBLIC_USER_NAME) != 0) { 
        status = clientLogin(conn);
        if (status != 0) {
            rodsLogError(LOG_ERROR, status, "uploadFile: ClientLogin error.");
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

    // clear buffer
    //_removeBufferredFile(iRODSPath);

    // Disconnect 
    rcDisconnect(conn);

    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: putUtil error.");
        return status;
    }

    return 0;
}

int
isLazyUploadBufferredFile (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "isLazyUploadBufferredFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "isLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _hasBufferredFile(iRODSPath);

    return status;
}

int
findLazyUploadBufferredFilePath (const char *path, char *lazyUploadPath) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "findLazyUploadBufferredFilePath: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "findLazyUploadBufferredFilePath: failed to get iRODS path - %s", path);
        return status;
    }

    return _getBufferPath(iRODSPath, lazyUploadPath);
}

/**************************************************************************
 * private functions
 **************************************************************************/
static int
_hasBufferredFile(const char *path) {
    int status;
    char bufferPath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_hasBufferredFile: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = _getBufferPath(path, bufferPath)) < 0) {
        rodsLogError(LOG_ERROR, status, "_hasBufferredFile: _getBufferPath error.");
        return status;
    }

    if ((status = stat(bufferPath, &stbufCache)) < 0) {
        //rodsLog (LOG_ERROR, "_hasBufferredFile: stat error for %s", bufferPath);
        //rodsLogError(LOG_ERROR, status, "_hasBufferredFile: stat error");
        return status;
    }

    return (0);
}

static int
_getBufferPath(const char *path, char *bufferPath) {
    if (path == NULL || bufferPath == NULL) {
        rodsLog (LOG_ERROR, "_getBufferPath: given path or bufferPath is NULL");
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
_removeBufferredFile(const char *path) {
    int status;
    char bufferPath[MAX_NAME_LEN];

    rodsLog (LOG_DEBUG, "_removeBufferredFile: %s", path);

    if ((status = _getBufferPath(path, bufferPath)) < 0) {
        return status;
    }

    status = unlink(bufferPath);
    return status;
}

static int
_getiRODSPath(const char *path, char *iRODSPath) {
    return getiRODSPath(path, iRODSPath, LazyUploadRodsEnv->rodsHome, LazyUploadRodsEnv->rodsCwd);
}
