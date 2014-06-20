#include <iostream>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include "irodsFs.h"
#include "iFuseLib.h"
#include "iFuseOper.h"
#include "hashtable.h"
#include "miscUtil.h"
#include "iFuseLib.Lock.h"
#include "getUtil.h"

/**************************************************************************
 * global variables
 **************************************************************************/
preloadConfig_t PreloadConfig;
rodsEnv *RodsEnv;
rodsArguments_t *RodsArgs;

concurrentList_t *PreloadThreadList;
Hashtable *PreloadThreadTable;

/**************************************************************************
 * function definitions
 **************************************************************************/
void *_preloadThread(void *arg);
int _download(const char *path, struct stat *stbufIn);
int _completeDownload(const char *workPath, const char *cachePath, struct stat *stbuf);
int _hasValidCache(const char *path, struct stat *stbuf);
int _getCachePath(const char *path, char *cachePath);
int _getiRODSPath(const char *path, char *iRODSPath);
int _hasCache(const char *path);
off_t _getFileSizeRecursive(const char *path);
int _invalidateCache(const char *path);
int _evictOldCache(off_t sizeNeeded);
int _findOldestCache(const char *path, char *oldCachePath, struct stat *oldStatbuf);
int _getCacheWorkPath(const char *path, char *cachePath);
int _preparePreloadCacheDir();
int _prepareDir(const char *path);
int _isDirectory(const char *path);
int _makeDirs(const char *path);
int _removeAllCaches();
int _removeDir(const char *path);
int _renameCache(const char *fromPath, const char *toPath);
int _truncateCache(const char *path, off_t size);
struct timeval _getCurrentTime();
double _timeDiff(struct timeval a, struct timeval b);
time_t _convTime(struct timeval);

/**************************************************************************
 * public functions
 **************************************************************************/
int
initPreload (preloadConfig_t *preloadConfig, rodsEnv *myRodsEnv, rodsArguments_t *myRodsArgs) {
    //char* icommandEnv;

    rodsLog (LOG_DEBUG, "initPreload: MyPreloadConfig.preload = %d", preloadConfig->preload);
    rodsLog (LOG_DEBUG, "initPreload: MyPreloadConfig.cachePath = %s", preloadConfig->cachePath);
    rodsLog (LOG_DEBUG, "initPreload: MyPreloadConfig.cacheMaxSize = %lld", preloadConfig->cacheMaxSize);

    // copy given configuration
    memcpy(&PreloadConfig, preloadConfig, sizeof(preloadConfig_t));
    RodsEnv = myRodsEnv;
    RodsArgs = myRodsArgs;

    // init hashtables
    PreloadThreadTable = newHashTable(NUM_PRELOAD_THREAD_HASH_SLOT);

    // init lists
    PreloadThreadList = newConcurrentList();

    // init lock
    INIT_LOCK(PreloadLock);

    _preparePreloadCacheDir();
    return (0);
}

int
uninitPreload (preloadConfig_t *preloadConfig) {
    int status;
    int i;
    int size;

    // destroy preload thread list
    size = listSize(PreloadThreadList);
    for(i=0;i<size;i++) {
        preloadThreadInfo_t *threadInfo = (preloadThreadInfo_t *)removeFirstElementOfConcurrentList(PreloadThreadList);
        if(threadInfo->running == PRELOAD_THREAD_RUNNING) {        
#ifdef USE_BOOST
            status = threadInfo->thread->join();
#else
            status = pthread_join(threadInfo->thread, NULL);
#endif
        }

        if(threadInfo->path != NULL) {
            free(threadInfo->path);
            threadInfo->path = NULL;
        }
    }

    FREE_LOCK(PreloadLock);
    return (0);
}

int
isPreloadEnabled() {
    // check whether preload is enabled
    if(PreloadConfig.preload == 0) {
        return -1;
    }
    return 0;
}

int
preloadFile (const char *path, struct stat *stbuf) {
    int status;
    preloadThreadInfo_t *threadInfo = NULL;
    preloadThreadData_t *threadData = NULL;
    char iRODSPath[MAX_NAME_LEN];
    off_t cacheSize;

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "preloadFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "preloadFile: failed to get iRODS path - %s", path);
        return status;
    }

    // check the given file is already preloaded
    if(_hasValidCache(iRODSPath, stbuf) != 0) {
        // invalidate cache - this may fail if cache file does not exists
        _invalidateCache(iRODSPath);

        // check whether preload cache exceeds limit
        if(PreloadConfig.cacheMaxSize > 0) {
            // cache max size is set
            if(stbuf->st_size > (off_t)PreloadConfig.cacheMaxSize) {
                rodsLog (LOG_DEBUG, "preloadFile: given file is bigger than cacheMaxSize, canceling preloading - %s", iRODSPath);
                return (0);
            }

            cacheSize = _getFileSizeRecursive(PreloadConfig.cachePath);
            if((cacheSize + stbuf->st_size) > (off_t)PreloadConfig.cacheMaxSize) {
                // evict?
                status = _evictOldCache((cacheSize + stbuf->st_size) - (off_t)PreloadConfig.cacheMaxSize);
                if(status < 0) {
                    rodsLog (LOG_ERROR, "preloadFile: failed to evict old cache");
                    return status;
                }
            }
        }

        // does not have valid cache
        // check the given file is preloading
        LOCK(PreloadLock);
        threadInfo = (preloadThreadInfo_t *)lookupFromHashTable(PreloadThreadTable, iRODSPath);
        if(threadInfo != NULL) {
            rodsLog (LOG_DEBUG, "preloadFile: preloading is already running - %s", iRODSPath);
            UNLOCK(PreloadLock);
            return 0;
        }

        // now, start a new preloading

        // create a new thread to preload
        threadInfo = (preloadThreadInfo_t *)malloc(sizeof(preloadThreadInfo_t));
        threadInfo->path = strdup(iRODSPath);
        threadInfo->running = PRELOAD_THREAD_RUNNING;
        INIT_STRUCT_LOCK((*threadInfo));

        insertIntoHashTable(PreloadThreadTable, iRODSPath, threadInfo);

        // prepare thread argument
        threadData = (preloadThreadData_t *)malloc(sizeof(preloadThreadData_t));
        threadData->path = strdup(iRODSPath);
        memcpy(&threadData->stbuf, stbuf, sizeof(struct stat));
        threadData->threadInfo = threadInfo;

        rodsLog (LOG_DEBUG, "preloadFile: start preloading - %s", iRODSPath);
#ifdef USE_BOOST
        status = threadInfo->thread = new boost::thread(_preloadThread, (void *)threadData);
#else
        status = pthread_create(&threadInfo->thread, NULL, _preloadThread, (void *)threadData);
#endif
        UNLOCK(PreloadLock);

        return status;
    } else {
        rodsLog (LOG_DEBUG, "preloadFile: given file is already preloaded - %s", iRODSPath);
        return (0);
    }
}

int
invalidatePreloadedCache (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "invalidatePreloadedCache: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "invalidatePreloadedCache: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(PreloadLock);

    status = _invalidateCache(iRODSPath);    

    UNLOCK(PreloadLock);

    return status;
}

int
renamePreloadedCache (const char *fromPath, const char *toPath) {
    int status;
    char fromiRODSPath[MAX_NAME_LEN];
    char toiRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(fromPath, fromiRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "renamePreloadedCache: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "renamePreloadedCache: failed to get iRODS path - %s", fromPath);
        return status;
    }

    status = _getiRODSPath(toPath, toiRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "renamePreloadedCache: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "renamePreloadedCache: failed to get iRODS path - %s", toPath);
        return status;
    }

    LOCK(PreloadLock);

    // directory
    status = _renameCache(fromiRODSPath, toiRODSPath);

    UNLOCK(PreloadLock);

    return status;
}

int
truncatePreloadedCache (const char *path, off_t size) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "truncatePreloadedCache: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "truncatePreloadedCache: failed to get iRODS path - %s", path);
        return status;
    }

    LOCK(PreloadLock);

    status = _truncateCache(iRODSPath, size);
    
    UNLOCK(PreloadLock);

    return status;
}

int
isPreloaded (const char *path) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "isPreloaded: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "isPreloaded: failed to get iRODS path - %s", path);
        return status;
    }

    return _hasCache(iRODSPath);
}

int
findPreloadPath (const char *path, char *preloadPath) {
    int status;
    char iRODSPath[MAX_NAME_LEN];

    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "findPreloadPath: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "findPreloadPath: failed to get iRODS path - %s", path);
        return status;
    }

    return _getCachePath(iRODSPath, preloadPath);
}

/**************************************************************************
 * private functions
 **************************************************************************/
void *_preloadThread(void *arg) {
    int status;
    preloadThreadData_t *threadData = (preloadThreadData_t *)arg;
    preloadThreadInfo_t *threadInfo = NULL;
    if(threadData == NULL) {
        rodsLog (LOG_ERROR, "_preloadThread: given thread argument is null");
        pthread_exit(NULL);
    }

    threadInfo = threadData->threadInfo;

    rodsLog (LOG_DEBUG, "_preloadThread: preload - %s", threadData->path);
    
    status = _download(threadData->path, &threadData->stbuf);
    if(status != 0) {
        rodsLog (LOG_DEBUG, "_preloadThread: download error - %d", status);
    }

    // downloading is done
    LOCK(PreloadLock);
    // change thread status
    LOCK_STRUCT(*threadInfo);
    threadInfo->running = PRELOAD_THREAD_IDLE;
    UNLOCK_STRUCT(*threadInfo);

    // release threadData
    rodsLog (LOG_DEBUG, "_preloadThread: thread finished - %s", threadData->path);
    if(threadData->path != NULL) {
        free(threadData->path);
        threadData->path = NULL;
    }

    free(threadData);

    // remove from hash table
    deleteFromHashTable(PreloadThreadTable, threadInfo->path);
    UNLOCK(PreloadLock);

    free(threadInfo);
    pthread_exit(NULL);
}

int
_download(const char *path, struct stat *stbufIn) {
    int status;
    rcComm_t *conn;
    rodsPathInp_t rodsPathInp;
    rErrMsg_t errMsg;
    char preloadCachePath[MAX_NAME_LEN];
    char preloadCacheWorkPath[MAX_NAME_LEN];

    // set path for getUtil
    status = _getCachePath(path, preloadCachePath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: _getCachePath error.");
        rodsLog (LOG_ERROR, "_download: failed to get cache path - %s", path);
        return status;
    }

    status = _getCacheWorkPath(path, preloadCacheWorkPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: _getCacheWorkPath error.");
        rodsLog (LOG_ERROR, "_download: failed to get cache work path - %s", path);
        return status;
    }

    rodsLog (LOG_DEBUG, "_download: download %s to %s", path, preloadCachePath);

    // set src path
    memset( &rodsPathInp, 0, sizeof( rodsPathInp_t ) );
    addSrcInPath( &rodsPathInp, (char*)path );
    status = parseRodsPath (&rodsPathInp.srcPath[0], RodsEnv);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: parseRodsPath error.");
        return status;
    }

    // set dest path
    rodsPathInp.destPath = ( rodsPath_t* )malloc( sizeof( rodsPath_t ) );
    memset( rodsPathInp.destPath, 0, sizeof( rodsPath_t ) );
    rstrcpy( rodsPathInp.destPath->inPath, preloadCacheWorkPath, MAX_NAME_LEN );
    status = parseLocalPath (rodsPathInp.destPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: parseLocalPath error.");
        return status;
    }

    // Connect
    conn = rcConnect (RodsEnv->rodsHost, RodsEnv->rodsPort, RodsEnv->rodsUserName, RodsEnv->rodsZone, RECONN_TIMEOUT, &errMsg);
    if (conn == NULL) {
        rodsLog (LOG_ERROR, "_download: error occurred while connecting to irods", path);
        return -1;
    }

    // Login
    if (strcmp (RodsEnv->rodsUserName, PUBLIC_USER_NAME) != 0) { 
        status = clientLogin(conn);
        if (status != 0) {
            rodsLogError(LOG_ERROR, status, "_download: ClientLogin error.");
            rcDisconnect(conn);
            return status;
        }
    }

    // make dir
    _prepareDir(preloadCachePath);

    // Preload
    rodsLog (LOG_DEBUG, "_download: download %s", path);
    status = getUtil (&conn, RodsEnv, RodsArgs, &rodsPathInp);
    rodsLog (LOG_DEBUG, "_download: complete downloading %s", path);

    // Disconnect 
    rcDisconnect(conn);

    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: getUtil error.");
        return status;
    }

    status = _completeDownload(preloadCacheWorkPath, preloadCachePath, stbufIn);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_download: _completeDownload error.");
        return status;
    }
    return 0;
}

int
_completeDownload(const char *workPath, const char *cachePath, struct stat *stbuf) {
    int status;
    struct utimbuf amtime;

    if (workPath == NULL || cachePath == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "_completeDownload: input workPath or cachePath or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    LOCK(PreloadLock);

    //amtime.actime = stbuf->st_atime;
    amtime.actime = _convTime(_getCurrentTime());
    amtime.modtime = stbuf->st_mtime;

    // set last access time and modified time the same as the original file
    status = utime(workPath, &amtime);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_completeDownload: utime error.");
        UNLOCK(PreloadLock);
        return status;
    }

    // change the name
    status = rename(workPath, cachePath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "_completeDownload: rename error.");
        UNLOCK(PreloadLock);
        return status;
    }

    UNLOCK(PreloadLock);
    return (0);
}

int
_hasCache(const char *path) {
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_hasCache: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    LOCK(PreloadLock);

	if ((status = _getCachePath(path, cachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    if ((status = stat(cachePath, &stbufCache)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    UNLOCK(PreloadLock);

    return (0);
}

off_t 
_getFileSizeRecursive(const char *path) {
    off_t accumulatedSize = 0;
    DIR *dir = NULL;
    char filepath[MAX_NAME_LEN];
    struct dirent *entry;
    struct stat statbuf;
    off_t localSize;

    LOCK(PreloadLock);

    dir = opendir(path);
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            snprintf(filepath, MAX_NAME_LEN, "%s/%s", path, entry->d_name);

            if (!stat(filepath, &statbuf)) {
                // has entry
                if (S_ISDIR(statbuf.st_mode)) {
                    // directory
                    localSize = _getFileSizeRecursive(filepath);
                    if(localSize > 0) {
                        accumulatedSize += localSize;
                    }
                } else {
                    // file
                    localSize = statbuf.st_size;
                    if(localSize > 0) {
                        accumulatedSize += localSize;
                    }
                }
            }
        }
        closedir(dir);
    }

    UNLOCK(PreloadLock);
    
    return accumulatedSize;
}

int
_hasValidCache(const char *path, struct stat *stbuf) {
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (path == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "_hasValidCache: input path or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    LOCK(PreloadLock);

	if ((status = _getCachePath(path, cachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    if ((status = stat(cachePath, &stbufCache)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    // compare stbufs
    if (stbufCache.st_size != stbuf->st_size) {
        UNLOCK(PreloadLock);
        return -1; // invalid size
    }

    if (stbufCache.st_mtim.tv_sec != stbuf->st_mtim.tv_sec) {
        UNLOCK(PreloadLock);
        return -1; // invalid mod time
    }

    if (stbufCache.st_mtim.tv_nsec != stbuf->st_mtim.tv_nsec) {
        UNLOCK(PreloadLock);
        return -1; // invalid mod time
    }

    UNLOCK(PreloadLock);
    return (0);
}

int
_invalidateCache(const char *path) {
    int status;
    char cachePath[MAX_NAME_LEN];
    char cacheWorkPath[MAX_NAME_LEN];

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_invalidateCache: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    LOCK(PreloadLock);

    if ((status = _getCacheWorkPath(path, cacheWorkPath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    if ((status = _getCachePath(path, cachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    if (_isDirectory(cachePath) == 0) {
        // directory
        status = _removeDir(cachePath);
    } else {
        // file
        // remove incomplete preload cache if exists
        unlink(cacheWorkPath);
        status = unlink(cachePath);
    }

    UNLOCK(PreloadLock);

    return status;
}

int
_findOldestCache(const char *path, char *oldCachePath, struct stat *oldStatbuf) {
    int status;    
    DIR *dir = NULL;
    char filepath[MAX_NAME_LEN];
    char tempPath[MAX_NAME_LEN];
    struct stat tempStatBuf;
    char oldestCachePath[MAX_NAME_LEN];
    struct stat oldestStatBuf;
    struct dirent *entry;
    struct stat statbuf;

    LOCK(PreloadLock);

    memset(oldestCachePath, 0, MAX_NAME_LEN);
    memset(&oldestStatBuf, 0, sizeof(struct stat));

    dir = opendir(path);
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            snprintf(filepath, MAX_NAME_LEN, "%s/%s", path, entry->d_name);
            
            if (!stat(filepath, &statbuf)) {
                // has entry
                if (S_ISDIR(statbuf.st_mode)) {
                    // directory
                    status = _findOldestCache(filepath, tempPath, &tempStatBuf);
                    if (status == 0) {
                        if (strlen(oldestCachePath) == 0) {
                            // just set
                            rstrcpy (oldestCachePath, tempPath, MAX_NAME_LEN);
                            memcpy (&oldestStatBuf, &tempStatBuf, sizeof(struct stat));
                        } else {
                            // compare
                            if(oldestStatBuf.st_atime > tempStatBuf.st_atime) {
                                rstrcpy (oldestCachePath, tempPath, MAX_NAME_LEN);
                                memcpy (&oldestStatBuf, &tempStatBuf, sizeof(struct stat));                                
                            }
                        }           
                    }
                } else {
                    // file
                    if (strlen(oldestCachePath) == 0) {
                        // just set
                        rstrcpy (oldestCachePath, filepath, MAX_NAME_LEN);
                        memcpy (&oldestStatBuf, &statbuf, sizeof(struct stat));
                    } else {
                        // compare
                        if(oldestStatBuf.st_atime > statbuf.st_atime) {
                            rstrcpy (oldestCachePath, filepath, MAX_NAME_LEN);
                            memcpy (&oldestStatBuf, &statbuf, sizeof(struct stat));                                
                        }
                    }
                }
            }
        }
        closedir(dir);
    }

    if (strlen(oldestCachePath) == 0) {
        UNLOCK(PreloadLock);
        return -1;
    }

    rstrcpy (oldCachePath, oldestCachePath, MAX_NAME_LEN);
    memcpy (oldStatbuf, &oldestStatBuf, sizeof(struct stat));   

    UNLOCK(PreloadLock);

    return (0);
}

int
_evictOldCache(off_t sizeNeeded) {
    int status;
    char oldCachePath[MAX_NAME_LEN];
    struct stat statbuf;
    off_t cacheSize = 0;
    off_t removedCacheSize = 0;

    if(sizeNeeded <= 0) {
        return 0;
    }

    LOCK(PreloadLock);

    while(sizeNeeded > removedCacheSize) {
        status = _findOldestCache(PreloadConfig.cachePath, oldCachePath, &statbuf);
        if(status < 0) {
            rodsLog (LOG_ERROR, "_evictOldCache: findOldestCache failed");
            UNLOCK(PreloadLock);
            return status;
        }

        if(statbuf.st_size > 0) {
            cacheSize = statbuf.st_size;
        } else {
            cacheSize = 0;
        }
    
        // remove
        status = unlink(oldCachePath);
        if(status < 0) {
            rodsLog (LOG_ERROR, "_evictOldCache: unlink failed - %s", oldCachePath);
            UNLOCK(PreloadLock);
            return status;
        }

        removedCacheSize += cacheSize;
    }

    UNLOCK(PreloadLock);
    
    return (0);
}

int
_getiRODSPath(const char *path, char *iRODSPath) {
    int len;
    char *tmpPtr1, *tmpPtr2;
    char tmpStr[MAX_NAME_LEN];

    if (path == NULL || iRODSPath == NULL) {
        rodsLog (LOG_ERROR, "_getiRODSPath: given path or iRODSPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    len = strlen (path);

    if (len == 0) {
    	/* just copy rodsCwd */
        rstrcpy (iRODSPath, RodsEnv->rodsCwd, MAX_NAME_LEN);
        return (0);
    } else if (strcmp (path, ".") == 0 || strcmp (path, "./") == 0) {
    	/* '.' or './' */
	    rstrcpy (iRODSPath, RodsEnv->rodsCwd, MAX_NAME_LEN);
    	return (0);
    } else if (strcmp (path, "~") == 0 || strcmp (path, "~/") == 0 || strcmp (path, "^") == 0 || strcmp (path, "^/") == 0) { 
    	/* ~ or ~/ */
        rstrcpy (iRODSPath, RodsEnv->rodsHome, MAX_NAME_LEN);
        return (0);
    } else if (path[0] == '~' || path[0] == '^') {
	    if (path[1] == '/') {
	        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", RodsEnv->rodsHome, path + 2);
	    } else {
	        /* treat it like a relative path */
	        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", RodsEnv->rodsCwd, path + 2);
	    }
    } else if (path[0] == '/') {
	    /* full path */
        //rstrcpy (iRODSPath, (char*)path, MAX_NAME_LEN);
        snprintf (iRODSPath, MAX_NAME_LEN, "%s%s", RodsEnv->rodsCwd, path);
    } else {
	    /* a relative path */
        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", RodsEnv->rodsCwd, path);
    }

    /* take out any "//" */
    while ((tmpPtr1 = strstr (iRODSPath, "//")) != NULL) {
        rstrcpy (tmpStr, tmpPtr1 + 2, MAX_NAME_LEN);
        rstrcpy (tmpPtr1 + 1, tmpStr, MAX_NAME_LEN);
    }

    /* take out any "/./" */
    while ((tmpPtr1 = strstr (iRODSPath, "/./")) != NULL) {
        rstrcpy (tmpStr, tmpPtr1 + 3, MAX_NAME_LEN);
        rstrcpy (tmpPtr1 + 1, tmpStr, MAX_NAME_LEN);
    }

    /* take out any /../ */
    while ((tmpPtr1 = strstr (iRODSPath, "/../")) != NULL) {  
	    /* go back */
	    tmpPtr2 = tmpPtr1 - 1;

	    while (*tmpPtr2 != '/') {
	        tmpPtr2 --;
	        if (tmpPtr2 < iRODSPath) {
		        rodsLog (LOG_ERROR, "_getiRODSPath: parsing error for %s", iRODSPath);
		        return (USER_INPUT_PATH_ERR);
	        }
	    }

        rstrcpy (tmpStr, tmpPtr1 + 4, MAX_NAME_LEN);
        rstrcpy (tmpPtr2 + 1, tmpStr, MAX_NAME_LEN);
    }

    /* handle "/.", "/.." and "/" at the end */

    len = strlen (iRODSPath);

    tmpPtr1 = iRODSPath + len;

    if ((tmpPtr2 = strstr (tmpPtr1 - 3, "/..")) != NULL) {
        /* go back */
        tmpPtr2 -= 1;
        while (*tmpPtr2 != '/') {
            tmpPtr2 --;
            if (tmpPtr2 < iRODSPath) {
                rodsLog (LOG_ERROR, "parseRodsPath: parsing error for %s", iRODSPath);
                return (USER_INPUT_PATH_ERR);
            }
        }

	    *tmpPtr2 = '\0';
	    if (tmpPtr2 == iRODSPath) {
            /* nothing, special case */
	        *tmpPtr2++ = '/'; /* root */
	        *tmpPtr2 = '\0';
	    }

        if (strlen(iRODSPath) >= MAX_PATH_ALLOWED-1) {
            return (USER_PATH_EXCEEDS_MAX);
        }
	    return (0);
    }

    /* take out "/." */
    if ((tmpPtr2 = strstr (tmpPtr1 - 2, "/.")) != NULL) {
        *tmpPtr2 = '\0';
        if (strlen(iRODSPath) >= MAX_PATH_ALLOWED-1) {
    	    return (USER_PATH_EXCEEDS_MAX);
        }
        return (0);
    }

    if (*(tmpPtr1 - 1) == '/' && len > 1) {
        *(tmpPtr1 - 1) = '\0';
        if (strlen(iRODSPath) >= MAX_PATH_ALLOWED-1) {
	        return (USER_PATH_EXCEEDS_MAX);
        }
        return (0);
    }

    if (strlen(iRODSPath) >= MAX_PATH_ALLOWED-1) {
       return (USER_PATH_EXCEEDS_MAX);
    }

    return (0);
}

int
_getCachePath(const char *path, char *cachePath) {
    if (path == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "_getCachePath: given path or cachePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if(strlen(path) > 0 && path[0] == '/') {
        snprintf(cachePath, MAX_NAME_LEN, "%s%s", PreloadConfig.cachePath, path);
    } else {
        snprintf(cachePath, MAX_NAME_LEN, "%s/%s", PreloadConfig.cachePath, path);
    }
    return (0);
}

int
_getCacheWorkPath(const char *path, char *cachePath) {
    if (path == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "_getCacheWorkPath: given path or cachePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if(strlen(path) > 0 && path[0] == '/') {
        snprintf(cachePath, MAX_NAME_LEN, "%s%s.part", PreloadConfig.cachePath, path);
    } else {
        snprintf(cachePath, MAX_NAME_LEN, "%s/%s.part", PreloadConfig.cachePath, path);
    }
    return (0);
}

int
_preparePreloadCacheDir() {
    int status;

    LOCK(PreloadLock);
    status = _makeDirs(PreloadConfig.cachePath);
    UNLOCK(PreloadLock);

    return status; 
}

int
_prepareDir(const char *path) {
    int status;
    char dir[MAX_NAME_LEN], file[MAX_NAME_LEN];
    
    if (path == NULL) {
        rodsLog (LOG_ERROR, "_prepareDir: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    LOCK(PreloadLock);
    splitPathByKey ((char *) path, dir, file, '/');
    
    // make dirs
    status = _makeDirs(dir);
    UNLOCK(PreloadLock);

    return status;
}

int
_isDirectory(const char *path) {
    struct stat stbuf;

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_isDirectory: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if ((stat(path, &stbuf) == 0) && (S_ISDIR(stbuf.st_mode))) {
        return (0);
    }

    return (-1);
}

int
_makeDirs(const char *path) {
    char dir[MAX_NAME_LEN];
    char file[MAX_NAME_LEN];
    int status;

    rodsLog (LOG_DEBUG, "_makeDirs: %s", path);

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_makeDirs: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) path, dir, file, '/');
    if(_isDirectory(dir) < 0) {
        // parent not exist
        status = _makeDirs(dir);

        if(status < 0) {
            return (status);
        }
    }
    
    // parent now exist
    status = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if(status == -EEXIST) {
        return (0);
    }
    return status;
}

int
_renameCache(const char *fromPath, const char *toPath) {
    int status;
    char fromCachePath[MAX_NAME_LEN];
    char toCachePath[MAX_NAME_LEN];

    rodsLog (LOG_DEBUG, "_renameCache: %s -> %s", fromPath, toPath);

    LOCK(PreloadLock);

    if ((status = _getCachePath(fromPath, fromCachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    if ((status = _getCachePath(toPath, toCachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    status = rename(fromCachePath, toCachePath);

    UNLOCK(PreloadLock);

    return status;
}

int
_truncateCache(const char *path, off_t size) {
    int status;
    char cachePath[MAX_NAME_LEN];

    rodsLog (LOG_DEBUG, "_truncateCache: %s, %ld", path, size);

    LOCK(PreloadLock);

    if ((status = _getCachePath(path, cachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    status = truncate(cachePath, size);

    UNLOCK(PreloadLock);

    return status;
}

int
_removeAllCaches() {
    int status;
    
    LOCK(PreloadLock);
    if((status = _removeDir(PreloadConfig.cachePath)) < 0) {
        UNLOCK(PreloadLock);
        return status;
    }

    UNLOCK(PreloadLock);

    return 0;
}

int
_removeDir(const char *path) {
    DIR *dir = opendir(path);
    char filepath[MAX_NAME_LEN];
    struct dirent *entry;
    struct stat statbuf;
    int status;
    int statusFailed = 0;

    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            snprintf(filepath, MAX_NAME_LEN, "%s/%s", path, entry->d_name);
            rodsLog (LOG_DEBUG, "_removeDir: removing : %s", dir);

            if (!stat(filepath, &statbuf)) {
                // has entry
                if (S_ISDIR(statbuf.st_mode)) {
                    // directory
                    status = _removeDir(filepath);
                    if(status < 0) {
                        statusFailed = status;
                    }
                } else {
                    // file
                    status = unlink(filepath);
                    if(status < 0) {
                        statusFailed = status;
                    }
                }
            }
        }
        closedir(dir);
    }

    if(statusFailed == 0) {
        statusFailed = rmdir(path);
    }

    return statusFailed;
}

struct timeval
_getCurrentTime() {
	struct timeval s_now;
	gettimeofday(&s_now, NULL);
	return s_now;
}

double
_timeDiff(struct timeval a, struct timeval b) {
	double time1;
	double time2;
	time1 = a.tv_sec + a.tv_usec / (double)1000000;
	time2 = b.tv_sec + b.tv_usec / (double)1000000;
	return time1 - time2;
}

time_t
_convTime(struct timeval a) {
    return (time_t)a.tv_sec;
}
