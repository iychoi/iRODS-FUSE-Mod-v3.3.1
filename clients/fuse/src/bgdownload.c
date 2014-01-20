#include "bgdownload.h"
#include <iostream>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "irodsFs.h"
#include "iFuseOper.h"
#include "miscUtil.h"

using namespace std;

typedef struct _thread_info
{
    pthread_t thread;
    char path[MAX_NAME_LEN];
    int running;
} thread_info;

thread_info threads[MAX_BG_THREADS];

typedef struct _thread_data
{
    int thread_no;
    char path[MAX_NAME_LEN];
    int flags;
} thread_data;

void *thread_func(void *arg);

int
initializeBgDownload()
{
    int i;
    for(i=0;i<MAX_BG_THREADS;i++) {
        memset(&threads[i], 0, sizeof(thread_info));
    }

    return (0);
}

int
startBgDownload(const char *path, int flags)
{
    int status;
    int i;
    thread_info *thread = NULL;
    thread_data param;

    //cout << "startBgDownload invoked" << endl;

    // check downloading is running
    for(i=0;i<MAX_BG_THREADS;i++) {
        thread_info *mythread = &threads[i];
        if(strcmp(mythread->path, path) == 0) {
            // same file is already in downloading
            return -1;
        }
    }

    // find idle thread
    for(i=0;i<MAX_BG_THREADS;i++) {
        thread_info *mythread = &threads[i];
        if(mythread->running == DOWNLOAD_THREAD_IDLE) {
            // idle
            thread = mythread;

            // filling thread_data
            param.thread_no = i;
            rstrcpy(param.path, (char*)path, MAX_NAME_LEN);
            param.flags = flags;
            
            break;
        }
    }

    if(thread == NULL) {
        return -1;
    }

    // prepare thread
    rstrcpy(thread->path, (char*)path, MAX_NAME_LEN);
    thread->running = DOWNLOAD_THREAD_RUNNING;

    status = pthread_create(&thread->thread, NULL, thread_func, (void *)&param);
    return (status);
    //thread_func(&param);
    //return (0);
}

void *thread_func(void *arg)
{
    int status;
    thread_data *param = (thread_data *)arg;
    thread_info *thread = &threads[param->thread_no];

    printf("thread run\n");

    status = _download(param->path, param->flags);
    if(status != 0) {
        printf("downloading error : %d\n", status);
    }

    thread->running = DOWNLOAD_THREAD_IDLE;
    memset(thread->path, 0, MAX_NAME_LEN);
    printf("thread gone\n");

    pthread_exit(NULL);
}

int
_download(const char *path, int flags)
{
    int status;
    dataObjInp_t dataObjInp;
    struct stat stbuf;
    char tempCachePath[MAX_NAME_LEN];
    char cachePath[MAX_NAME_LEN];
    char objPath[MAX_NAME_LEN];
    iFuseConn_t *iFuseConn = NULL;
    //pathCache_t *tmpPathCache = NULL;
    //iFuseDesc_t *desc = NULL;

    printf("downloading file %s\n", path);

    memset (&dataObjInp, 0, sizeof (dataObjInp));
	dataObjInp.openFlags = flags;

	status = parseRodsPathStr ((char *) (path + 1) , &MyRodsEnv, objPath);
	rstrcpy(dataObjInp.objPath, objPath, MAX_NAME_LEN);
	if (status < 0) {
		rodsLogError (LOG_ERROR, status, "irodsOpen: parseRodsPathStr of %s error", path);
		/* use ENOTDIR for this type of error */
		return -ENOTDIR;
	}

	iFuseConn = getAndUseConnByPath((char *) path, &MyRodsEnv, &status);
	/* status = getAndUseIFuseConn(&iFuseConn, &MyRodsEnv); */
	if(status < 0) {
		rodsLogError (LOG_ERROR, status, "irodsOpen: cannot get connection for %s error", path);
		/* use ENOTDIR for this type of error */
		return -ENOTDIR;
	}

	status = _irodsGetattr (iFuseConn, path, &stbuf);

    if ((status = checkCacheExistance(path, &stbuf)) == 0) {
        // have cache
        printf("cache exists %s\n", path);
        unuseIFuseConn(iFuseConn);
        return 0;
    }

    printf("cache not exists %s\n", path);
    // invalidate cache
    invalidateCacheFile(path);

    // start download
    rodsLog (LOG_DEBUG, "irodsOpenWithReadCache: caching %s", path);
	if ((status = getTempDownloadCachePath (path, tempCachePath)) < 0) {
		unuseIFuseConn(iFuseConn);
		return status;
	}

    if ((status = getDownloadCachePath (path, cachePath)) < 0) {
		unuseIFuseConn(iFuseConn);
        return status;
    }
    
    // make cache dir
    printf("making cache path %s\n", cachePath);
    makeDownloadCacheDir(cachePath);

	/* get the file to local cache */
	dataObjInp.dataSize = stbuf.st_size;

	status = rcDataObjGet (iFuseConn->conn, &dataObjInp, tempCachePath);
	unuseIFuseConn(iFuseConn);

	if (status < 0) {
		rodsLogError (LOG_ERROR, status, "irodsOpenWithReadCache: rcDataObjGet of %s error", dataObjInp.objPath);
		return status;
	}

    completeFileDownload(tempCachePath, cachePath, &stbuf);
    
    /*
    int fd = open(cachePath, O_RDWR);

	fileCache_t *fileCache = addFileCache(fd, objPath, (char *) path, cachePath, stbuf.st_mode, HAVE_READ_CACHE);
    matchAndLockPathCache((char *) path, &tmpPathCache);
	if(tmpPathCache == NULL) {
		pathExist((char *) path, fileCache, &stbuf, NULL);
	} else {
		_addFileCacheForPath(tmpPathCache, fileCache);
		UNLOCK_STRUCT(*tmpPathCache);
	}

    desc = newIFuseDesc(objPath, (char *) path,fileCache, &status);
	if (status < 0) {
		rodsLogError (LOG_ERROR, status, "irodsOpen: create descriptor of %s error", dataObjInp.objPath);
		return status;
	}
    */
    return (0);
}

int
completeFileDownload(const char *inPath, const char *destPath, struct stat *stbuf)
{
    int status;
    struct utimbuf amtime;

    if (inPath == NULL || destPath == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "completeFileDownload: input inPath or destPath or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    amtime.actime = stbuf->st_atime;
    amtime.modtime = stbuf->st_mtime;    

    status = utime(inPath, &amtime);
    if(status < 0) {
        return status;
    }

    return rename(inPath, destPath);
}

int
checkCacheExistanceNoCheck(const char *inPath)
{
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (inPath == NULL) {
        rodsLog (LOG_ERROR, "checkCacheExistance: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = getDownloadCachePath (inPath, cachePath)) < 0) {
        return status;
    }

    if ((status = stat(cachePath, &stbufCache)) < 0) {
        return status;
    }

    return (0);
}

int
checkCacheExistance(const char *inPath, struct stat *stbuf)
{
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (inPath == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "checkCacheExistance: input inPath or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = getDownloadCachePath (inPath, cachePath)) < 0) {
        return status;
    }

    if ((status = stat(cachePath, &stbufCache)) < 0) {
        return status;
    }

    // compare stbufs
    if (stbufCache.st_size != stbuf->st_size) {
        return -1; // invalid size
    }

    if (stbufCache.st_mtim.tv_sec != stbuf->st_mtim.tv_sec) {
        return -1; // invalid mod time
    }

    if (stbufCache.st_mtim.tv_nsec != stbuf->st_mtim.tv_nsec) {
        return -1; // invalid mod time
    }

    return (0);
}

int
invalidateCacheFile(const char *inPath) {
    int status;
    char cachePath[MAX_NAME_LEN];

    if (inPath == NULL) {
        rodsLog (LOG_ERROR, "invalidateCacheFile: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if ((status = getDownloadCachePath (inPath, cachePath)) < 0) {
        return status;
    }

    return unlink(cachePath);
}

int
getDownloadCachePath(const char *inPath, char *cachePath)
{
    if (inPath == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "getDownloadCachePath: input inPath or cachePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if(strlen(inPath) > 0 && inPath[0] == '/') {
        snprintf(cachePath, MAX_NAME_LEN, "%s%s", BGDOWNLOAD_CACHE_PATH, inPath);
    } else {
        snprintf(cachePath, MAX_NAME_LEN, "%s/%s", BGDOWNLOAD_CACHE_PATH, inPath);
    }
    return (0);
}

int
getTempDownloadCachePath(const char *inPath, char *cachePath)
{
    if (inPath == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "getTempDownloadCachePath: input inPath or cachePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if(strlen(inPath) > 0 && inPath[0] == '/') {
        snprintf(cachePath, MAX_NAME_LEN, "%s%s.part", BGDOWNLOAD_CACHE_PATH, inPath);
    } else {
        snprintf(cachePath, MAX_NAME_LEN, "%s/%s.part", BGDOWNLOAD_CACHE_PATH, inPath);
    }
    return (0);
}

int
makeDownloadCacheDir(const char *filePath)
{
    int status;
    char myDir[MAX_NAME_LEN], myFile[MAX_NAME_LEN];
    
    if (filePath == NULL) {
        rodsLog (LOG_ERROR, "makeDownloadCacheDir: input filePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) filePath, myDir, myFile, '/');
    
    // make dirs
    status = makeDirs(myDir);
    return status;
}

int
isDirectory(const char *path)
{
    struct stat stbuf;

    if (path == NULL) {
        rodsLog (LOG_ERROR, "isDirectory: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if ((stat(path, &stbuf) == 0) && (((stbuf.st_mode) & S_IFMT) == S_IFDIR)) {
        return (0);
    }

    return (-1);
}

int
makeDirs(const char *path)
{
    char myDir[MAX_NAME_LEN], myFile[MAX_NAME_LEN];
    int status;

    printf("makeDirs : %s\n", path);

    if (path == NULL) {
        rodsLog (LOG_ERROR, "makeDirs: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) path, myDir, myFile, '/');
    if(isDirectory(myDir) < 0) {
        // parent not exist
        status = makeDirs(myDir);

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
