/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/* iFuseLib.h - Header for for iFuseLib.c */

#ifndef I_FUSE_LIB_H
#define I_FUSE_LIB_H

#include "rodsClient.h"
#include "rodsPath.h"

#define CACHE_FUSE_PATH         1
#ifdef CACHE_FUSE_PATH
#define CACHE_FILE_FOR_READ     1
#define CACHE_FILE_FOR_NEWLY_CREATED     1
#endif

#define ENABLE_PRELOAD_AND_LAZY_UPLOAD          1

#define MAX_BUF_CACHE   2
#define MAX_IFUSE_DESC   512
#define MAX_READ_CACHE_SIZE   (1024*1024)	/* 1 mb */
#define MAX_NEWLY_CREATED_CACHE_SIZE   (4*1024*1024)	/* 4 mb */
#define HIGH_NUM_CONN	5	/* high water mark */
#define MAX_NUM_CONN	10

#define NUM_NEWLY_CREATED_SLOT	5
#define MAX_NEWLY_CREATED_TIME	5	/* in sec */

#define FUSE_CACHE_DIR	"/tmp/fuseCache"
#define FUSE_PRELOAD_CACHE_DIR  "/tmp/fusePreloadCache"
#define FUSE_LAZY_UPLOAD_BUFFER_DIR  "/tmp/fuseLazyUploadBuffer"

#define IRODS_FREE		0
#define IRODS_INUSE	1 


#ifdef USE_BOOST
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#else
#include <pthread.h>
#endif
// =-=-=-=-=-=-=-

typedef struct BufCache {
    rodsLong_t beginOffset;
    rodsLong_t endOffset;
    void *buf;
} bufCache_t;

typedef enum { 
    NO_FILE_CACHE, /* no cache, file has already been deleted */
    HAVE_READ_CACHE, /* has cache, same as server copy */
    HAVE_NEWLY_CREATED_CACHE, /* has cache, updated from server copy */
} cacheState_t;

typedef struct ConnReqWait {
#ifdef USE_BOOST
    boost::mutex* mutex;
    boost::condition_variable cond;
#else
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
    int state;
} connReqWait_t;

typedef struct PathCache pathCache_t;
typedef struct newlyCreatedFile fileCache_t;

typedef struct IFuseDesc {
    bufCache_t  bufCache[MAX_BUF_CACHE];
    fileCache_t *fileCache;    /* (cacheInx + 1) currently active. 0 means no cache */
    rodsLong_t offset;
    rodsLong_t bytesWritten;
    char *objPath;
    char *localPath;
    int index;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} iFuseDesc_t;

#define NUM_PATH_HASH_SLOT	201
#define CACHE_EXPIRE_TIME	600	/* 10 minutes before expiration */

typedef struct PathCache {
    iFuseConn_t *iFuseConn;
    char* localPath;
    struct stat stbuf;
    uint cachedTime;
    int expired;
    fileCache_t *fileCache;
    iFuseDesc_t *desc;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} pathCache_t;

typedef struct PathCacheQue {
    pathCache_t *top;
    pathCache_t *bottom;
} pathCacheQue_t;

typedef struct specialPath {
    char *path;
    int len;
} specialPath_t;

typedef struct newlyCreatedFile {
    int iFd;    /* irods client fd */
    rodsLong_t offset;
    int status;
    /* int inuseFlag; */      /* 0 means not in use */
    char *fileCachePath;
    uint cachedTime;
    cacheState_t state;
    rodsLong_t fileSize;
    char *localPath;
    char *objPath;
    int mode;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} fileCache_t;

#define FUSE_FILE_CACHE_FREE(c) ((c).desc == NULL && (c).pathCache == NULL)

#define FUSE_FILE_CACHE_EXPIRED(cacheTime, c) (cachedTime - (c).cachedTime  >= MAX_NEWLY_CREATED_TIME)

typedef struct PreloadConfig {
    int preload;
    int clearCache;
    char *cachePath;
    rodsLong_t cacheMaxSize; /* 0 means unlimited */
    rodsLong_t preloadMinSize; /* 0 means "use default" */ 
} preloadConfig_t;

typedef struct LazyUploadConfig {
    int lazyUpload;
    char *bufferPath;
} lazyUploadConfig_t;

#define PRELOAD_FILES_IN_DOWNLOADING_EXT    ".part"
#define NUM_PRELOAD_THREAD_HASH_SLOT	201
#define NUM_LAZYUPLOAD_FILE_HASH_SLOT   201

typedef struct PreloadThreadInfo {
#ifdef USE_BOOST
    boost::thread* thread;
#else
    pthread_t thread;
#endif
    char *path;
    int running;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} preloadThreadInfo_t;

#define PRELOAD_THREAD_RUNNING    1
#define PRELOAD_THREAD_IDLE    0

typedef struct LazyUploadFileInfo {
    char *path;
    int accmode;
    int handle;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} lazyUploadFileInfo_t;

typedef struct PreloadThreadData {
    char *path;
    struct stat stbuf;
    preloadThreadInfo_t *threadInfo;
} preloadThreadData_t;

#define NUM_PRELOAD_FILEHANDLE_HASH_SLOT    201

typedef struct PreloadFileHandleInfo {
    char *path;
    int handle;
#ifdef USE_BOOST
    boost::mutex* mutex;
#else
    pthread_mutex_t lock;
#endif
} preloadFileHandleInfo_t;

#ifdef  __cplusplus
extern "C" {
#endif

int
initIFuseDesc ();
int initFileCache();
void initConn();
int
lockDesc (int descInx);
int
unlockDesc (int descInx);
int
iFuseConnInuse (iFuseConn_t *iFuseConn);
iFuseConn_t *
getIFuseConnByRcConn (rcComm_t *conn);
int
getIFuseConnByPath (iFuseConn_t **iFuseConn, char *localPath,
rodsEnv *myRodsEnv);
int
_getIFuseConn (iFuseConn_t **iFuseConn, rodsEnv *MyRodsEnv);
int
useIFuseConn (iFuseConn_t *iFuseConn);
int
_useFreeIFuseConn (iFuseConn_t *iFuseConn);
int
_useIFuseConn (iFuseConn_t *iFuseConn);
int
unuseIFuseConn (iFuseConn_t *iFuseConn);
int 
useConn (rcComm_t *conn);
void
connManager ();
int
disconnectAll ();
int
signalConnManager ();
int
getNumConn ();
int
iFuseDescInuse ();
int
checkFuseDesc (int descInx);
int
initPathCache ();
int
getHashSlot (int value, int numHashSlot);
int
chkCacheExpire (pathCacheQue_t *pathCacheQue);
int
addPathToCache (char *inPath, fileCache_t *fileCache, Hashtable *pathQueArray,
struct stat *stbuf, pathCache_t **outPathCache);
int
_addPathToCache (char *inPath, fileCache_t *fileCache, Hashtable *pathQueArray,
struct stat *stbuf, pathCache_t **outPathCache);
int
addToCacheSlot (char *inPath, char *fileCachePath, pathCacheQue_t *pathCacheQue,
struct stat *stbuf, pathCache_t **outPathCache);
int
pathSum (char *inPath);
int
isSpecialPath (char *inPath);
int
rmPathFromCache (char *inPath, Hashtable *pathQueArray);
int
_rmPathFromCache (char *inPath, Hashtable *pathQueArray);
int
addNewlyCreatedToCache (char *path, char *localPath, int descInx, int mode,
pathCache_t **tmpPathCache);
int
ifuseFileCacheSwapOut (fileCache_t *newlyCreatedFile);
int
closeIrodsFd (rcComm_t *conn, int fd);
int
getDescInxInNewlyCreatedCache (char *path, int flags);
int
fillDirStat (struct stat *stbuf, uint ctime, uint mtime, uint atime);
int
fillFileStat (struct stat *stbuf, uint mode, rodsLong_t size, uint ctime,
uint mtime, uint atime);
int
irodsMknodWithCache (char *path, mode_t mode, char *cachePath);
int
irodsOpenWithFileCache (iFuseConn_t *iFuseConn, char *path, int flags);
int
getFileCachePath (const char *inPath, char *cacehPath);
int
setAndMkFileCacheDir ();
int
ifuseClose (iFuseDesc_t *desc);
int
ifuseFlush (int descInx);
int
_ifuseFlush (iFuseDesc_t *desc);
int
dataObjCreateByFusePath (rcComm_t *conn, char *path, int mode, 
char *irodsPath);
int
ifusePut (rcComm_t *conn, char *objPath, char *cachePath, int mode,
rodsLong_t srcSize);
int
freeFileCache (pathCache_t *tmpPathCache);
int
ifuseReconnect (iFuseConn_t *iFuseConn);
int
ifuseConnect (iFuseConn_t *iFuseConn, rodsEnv *myRodsEnv);
int
getNewlyCreatedDescByPath (char *path);
int
renmeLocalPath (char *from, char *to, char *toIrodsPath);
int	_chkCacheExpire (pathCacheQue_t *pathCacheQue);
int _matchAndLockPathCache (char *inPath, pathCacheQue_t *pathQueArray, pathCache_t **outPathCache);
int _iFuseConnInuse (iFuseConn_t *iFuseConn);
int
initPreload (preloadConfig_t *preloadConfig, rodsEnv *myRodsEnv, rodsArguments_t *myRodsArgs);
int
uninitPreload (preloadConfig_t *preloadConfig);
int
isPreloadEnabled();
int
preloadFile (const char *path, struct stat *stbuf);
int
invalidatePreloadedCache (const char *path);
int
renamePreloadedCache (const char *fromPath, const char *toPath);
int
truncatePreloadedCache (const char *path, off_t size);
int
isPreloaded (const char *path);
int
openPreloadedFile (const char *path);
int
readPreloadedFile (int fileDesc, char *buf, size_t size, off_t offset);
int
closePreloadedFile (const char *path);
int
moveToPreloadedDir (const char *path, const char *iRODSPath);
int
initLazyUpload (lazyUploadConfig_t *lazyUploadConfig, rodsEnv *myRodsEnv, rodsArguments_t *myRodsArgs);
int
uninitLazyUpload (lazyUploadConfig_t *lazyUploadConfig);
int
isLazyUploadEnabled ();
int
isLazyUploadBufferred (const char *path);
int
mknodLazyUploadBufferredFile (const char *path);
int
openLazyUploadBufferredFile (const char *path, int accmode);
int
writeLazyUploadBufferredFile (const char *path, const char *buf, size_t size, off_t offset);
int
closeLazyUploadBufferredFile (const char *path);
int
uploadFile (const char *path);
#ifdef  __cplusplus
}
#endif

#endif	/* I_FUSE_LIB_H */
