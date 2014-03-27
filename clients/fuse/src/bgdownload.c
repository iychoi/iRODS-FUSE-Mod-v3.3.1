#include "bgdownload.h"
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
#include "iFuseOper.h"
#include "miscUtil.h"

// structures
typedef struct _bgdn_cache_info
{
    char path[MAX_NAME_LEN];
    off_t size;
    timeval lastAccess;
} bgdn_cache_info;

bgdn_cache_info caches[MAX_NUM_OF_CACHES];

typedef struct _bgdn_thread_info
{
    pthread_t thread;
    char path[MAX_NAME_LEN];
    int running;
} bgdn_thread_info;

bgdn_thread_info threads[MAX_BG_THREADS];
pthread_mutex_t threadLock;

typedef struct _bgdn_thread_data
{
    int thread_no;
    char path[MAX_NAME_LEN];
    struct stat stbuf;
} bgdn_thread_data;

char igetPath[MAX_NAME_LEN];


// function definitions
void *_downloadThread(void *arg);
int _download(const char *path, struct stat *stbufIn);
int _completeDownload(const char *inPath, const char *destPath, struct stat *stbuf);
int _hasValidCache(const char *inPath, struct stat *stbuf);
int _getCachePath(const char *inPath, char *cachePath);
int _hasCache(const char *inPath);
int _invalidateCache(const char *inPath);
int _getCacheDownloadingPath(const char *inPath, char *cachePath);
int _prepareCacheDir();
int _prepareDir(const char *filePath);
int _isDirectory(const char *path);
int _makeDirs(const char *path);
int _removeAllCaches();
int _removeDir(const char *path);
unsigned long _getCacheFreeSpace();
struct timeval _getCurrentTime();
double _timeDiff(struct timeval a, struct timeval b);

// implementations

int
bgdnInitialize()
{
    int i;
    char* icommandEnv;

    for(i=0;i<MAX_NUM_OF_CACHES;i++) {
        memset(&caches[i], 0, sizeof(bgdn_cache_info));
    }

    for(i=0;i<MAX_BG_THREADS;i++) {
        memset(&threads[i], 0, sizeof(bgdn_thread_info));
    }

    pthread_mutex_init(&threadLock, NULL);

    _prepareCacheDir();

    // find iget path
    icommandEnv = getenv(ICOMMAND_PATH_ENVIRONMENT);
    if(icommandEnv != NULL && strlen(icommandEnv) > 0) {
        if(icommandEnv[strlen(icommandEnv)-1] == '/') {
            snprintf(igetPath, MAX_NAME_LEN, "%s%s", icommandEnv, ICOMMAND_IGET_NAME);
        } else {
            snprintf(igetPath, MAX_NAME_LEN, "%s/%s", icommandEnv, ICOMMAND_IGET_NAME);
        }
        bgdn_log("bgdnInitialize: found iget (icommand) path : %s\n", igetPath);
    } else {
        snprintf(igetPath, MAX_NAME_LEN, "%s", ICOMMAND_IGET_NAME);
        bgdn_log("bgdnInitialize: failed to find iget (icommand) path -- using default : %s\n", igetPath);
    }

    return (0);
}

int
bgdnUninitialize()
{
    int status;
    int i;

    status = pthread_mutex_destroy(&threadLock);

    for(i=0;i<MAX_BG_THREADS;i++) {
        if(threads[i].running == DOWNLOAD_THREAD_RUNNING) {
            status = pthread_join(threads[i].thread, NULL);
        }
    }

    // remove all caches
    _removeAllCaches();

    return (0);
}

int
bgdnDownload(const char *path, struct stat *stbuf)
{
    int status;
    int i;
    bgdn_thread_info *thread = NULL;
    bgdn_thread_data param;

    //cout << "startBgDownload invoked" << endl;
    bgdn_log("bgdnDownload: download started\n");

    // check downloading is running
    status = pthread_mutex_lock(&threadLock);

    for(i=0;i<MAX_BG_THREADS;i++) {
        bgdn_thread_info *mythread = &threads[i];
        if(strcmp(mythread->path, path) == 0) {
            // same file is already in downloading
            status = pthread_mutex_unlock(&threadLock);
            bgdn_log("bgdnDownload: download is already running\n");
            return -1;
        }
    }

    // find idle thread
    for(i=0;i<MAX_BG_THREADS;i++) {
        bgdn_thread_info *mythread = &threads[i];
        if(mythread->running == DOWNLOAD_THREAD_IDLE) {
            // idle
            thread = mythread;

            // filling thread_data
            param.thread_no = i;
            rstrcpy(param.path, (char*)path, MAX_NAME_LEN);
            memcpy(&param.stbuf, stbuf, sizeof(struct stat));
            break;
        }
    }

    if(thread == NULL) {
        bgdn_log("bgdnDownload: could not create a new thread\n");
        return -1;
    }

    // prepare thread
    rstrcpy(thread->path, (char*)path, MAX_NAME_LEN);
    thread->running = DOWNLOAD_THREAD_RUNNING;
    status = pthread_mutex_unlock(&threadLock);

    status = pthread_create(&thread->thread, NULL, _downloadThread, (void *)&param);
    return (status);
}

int
bgdnHasCache(const char *inPath)
{
    return _hasCache(inPath);
}

int
bgdnGetCachePath(const char *inPath, char *cachePath)
{
    return _getCachePath(inPath, cachePath);
}

// private functions
void *_downloadThread(void *arg)
{
    int status;
    bgdn_thread_data *param = (bgdn_thread_data *)arg;
    bgdn_thread_info *thread = &threads[param->thread_no];

    bgdn_log("_downloadThread: new thread is now running - %s\n", param->path);

    status = _download(param->path, &param->stbuf);
    if(status != 0) {
        bgdn_log("_downloadThread: download error - %d\n", status);
    }

    // chage thread state
    status = pthread_mutex_lock(&threadLock);
    thread->running = DOWNLOAD_THREAD_IDLE;
    memset(thread->path, 0, MAX_NAME_LEN);
    bgdn_log("_downloadThread: thread finished - %s\n", param->path);
    status = pthread_mutex_unlock(&threadLock);

    pthread_exit(NULL);
}

int
_download(const char *path, struct stat *stbufIn)
{
    int status;
    struct stat stbuf;
    char tempCachePath[MAX_NAME_LEN];
    char cachePath[MAX_NAME_LEN];
    off_t freespc;

    bgdn_log("_download: downloading file - %s\n", path);

    memcpy(&stbuf, stbufIn, sizeof(struct stat));

	if ((status = _hasValidCache(path, &stbuf)) == 0) {
        // have cache
        return 0;
    }

    // invalidate cache - this may fail if cache file does not exists
    _invalidateCache(path);

    freespc = _getCacheFreeSpace();
    if(freespc < stbuf.st_size) {
        bgdn_log("_download: cache space not enough - %lu (%lu in free)\n", stbuf.st_size, freespc);
        return -ENOSPC;
    }

    // start download
    rodsLog (LOG_DEBUG, "_download: caching %s", path);
    bgdn_log("_download: caching - %s\n", path);
	if ((status = _getCacheDownloadingPath(path, tempCachePath)) < 0) {
		return status;
	}

    if ((status = _getCachePath(path, cachePath)) < 0) {
        return status;
    }
    
    // make cache dir
    _prepareDir(cachePath);

    // actual download
    bgdn_log("_download: start iget - %s\n", path);
    char commandBuffer[MAX_NAME_LEN];
    snprintf(commandBuffer, MAX_NAME_LEN, "iget -f -V %s %s", path+1, tempCachePath);
    status = system(commandBuffer);

    bgdn_log("_download: complete iget - %s\n", path);
    
    return _completeDownload(tempCachePath, cachePath, &stbuf);
}

int
_completeDownload(const char *inPath, const char *destPath, struct stat *stbuf)
{
    int status;
    struct utimbuf amtime;
    int i;
    bgdn_cache_info *emptyInfo = NULL;
    bgdn_cache_info *oldestInfo = NULL;

    if (inPath == NULL || destPath == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "_completeDownload: input inPath or destPath or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    amtime.actime = stbuf->st_atime;
    amtime.modtime = stbuf->st_mtime;

    // set last access time and modified time the same as the original file
    status = utime(inPath, &amtime);
    if(status < 0) {
        return status;
    }

    // change the name
    status = rename(inPath, destPath);
    if(status < 0) {
        return status;
    }

    // register to the array
    for(i=0;i<MAX_NUM_OF_CACHES;i++) {
        bgdn_cache_info *myInfo = &caches[i];
        if(myInfo->size == 0) {
            // found empty
            emptyInfo = myInfo;
            break;
        } else {
            if(oldestInfo == NULL) {
                oldestInfo = myInfo;
            } else {
                if(_timeDiff(oldestInfo->lastAccess, myInfo->lastAccess) > 0) {
                    oldestInfo = myInfo;
                }
            }
        }
    }

    if(emptyInfo != NULL) {
        // has empty
        rstrcpy(emptyInfo->path, (char*)destPath, MAX_NAME_LEN);
        emptyInfo->size = stbuf->st_size;
        emptyInfo->lastAccess = _getCurrentTime();
    } else {
        // evict cache
        if(oldestInfo != NULL && strlen(oldestInfo->path) > 0) {
            unlink(oldestInfo->path);
            rstrcpy(oldestInfo->path, (char*)destPath, MAX_NAME_LEN);
            oldestInfo->size = stbuf->st_size;
            oldestInfo->lastAccess = _getCurrentTime();
        }
    }
    return (0);
}

int
_hasCache(const char *inPath)
{
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (inPath == NULL) {
        rodsLog (LOG_ERROR, "_hasCache: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = _getCachePath(inPath, cachePath)) < 0) {
        return status;
    }

    if ((status = stat(cachePath, &stbufCache)) < 0) {
        return status;
    }

    return (0);
}

int
_hasValidCache(const char *inPath, struct stat *stbuf)
{
    int status;
    char cachePath[MAX_NAME_LEN];
    struct stat stbufCache;

    if (inPath == NULL || stbuf == NULL) {
        rodsLog (LOG_ERROR, "_hasValidCache: input inPath or stbuf is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

	if ((status = _getCachePath(inPath, cachePath)) < 0) {
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
_invalidateCache(const char *inPath) {
    int status;
    char cachePath[MAX_NAME_LEN];

    if (inPath == NULL) {
        rodsLog (LOG_ERROR, "_invalidateCache: input inPath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    if ((status = _getCachePath(inPath, cachePath)) < 0) {
        return status;
    }

    return unlink(cachePath);
}

int
_getCachePath(const char *inPath, char *cachePath)
{
    if (inPath == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "_getCachePath: input inPath or cachePath is NULL");
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
_getCacheDownloadingPath(const char *inPath, char *cachePath)
{
    if (inPath == NULL || cachePath == NULL) {
        rodsLog (LOG_ERROR, "_getTempDownloadCachePath: input inPath or cachePath is NULL");
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
_prepareCacheDir()
{
    int status;
    status = _makeDirs(BGDOWNLOAD_CACHE_PATH);   
    return status; 
}

int
_prepareDir(const char *filePath)
{
    int status;
    char myDir[MAX_NAME_LEN], myFile[MAX_NAME_LEN];
    
    if (filePath == NULL) {
        rodsLog (LOG_ERROR, "_prepareDir: input filePath is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) filePath, myDir, myFile, '/');
    
    // make dirs
    status = _makeDirs(myDir);
    return status;
}

int
_isDirectory(const char *path)
{
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
_makeDirs(const char *path)
{
    char myDir[MAX_NAME_LEN], myFile[MAX_NAME_LEN];
    int status;

    bgdn_log("_makeDirs: %s\n", path);

    if (path == NULL) {
        rodsLog (LOG_ERROR, "_makeDirs: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) path, myDir, myFile, '/');
    if(_isDirectory(myDir) < 0) {
        // parent not exist
        status = _makeDirs(myDir);

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
_removeAllCaches()
{
    return _removeDir(BGDOWNLOAD_CACHE_PATH);
}

int
_removeDir(const char *path)
{
    DIR *dir = opendir(path);
    char myDir[MAX_NAME_LEN];
    struct dirent *entry;
    struct stat statbuf;
    int status;
    int statusFailed = 0;

    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            snprintf(myDir, MAX_NAME_LEN, "%s/%s", path, entry->d_name);
            bgdn_log("_removeDir: removing : %s\n", myDir);

            if (!stat(myDir, &statbuf)) {
                // has entry
                if (S_ISDIR(statbuf.st_mode)) {
                    // directory
                    status = _removeDir(myDir);
                    if(status < 0) {
                        statusFailed = status;
                    }
                } else {
                    // file
                    status = unlink(myDir);
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

unsigned long
_getCacheFreeSpace()
{
    int status;
    struct statvfs buf;
    char myDir[MAX_NAME_LEN];

    snprintf(myDir, MAX_NAME_LEN, "%s/", BGDOWNLOAD_CACHE_PARENT_PATH);
    //bgdn_log("_getCacheFreeSpace: check size %s\n", myDir);

    if ((status = statvfs(myDir, &buf)) == 0) {
        unsigned long blksize, freeblks, free;

        blksize = buf.f_bsize;
        freeblks = buf.f_bfree;
        free = freeblks * blksize;
        //bgdn_log("_getCacheFreeSpace: blksize = %lu\n", blksize);
        //bgdn_log("_getCacheFreeSpace: freeblks = %lu\n", freeblks);
        //bgdn_log("_getCacheFreeSpace: free = %lu\n", free);
        return free;
    }

    bgdn_log("_getCacheFreeSpace: err : %d(%d)\n", status, errno);
    return 0;
}

struct timeval
_getCurrentTime()
{
	struct timeval s_now;
	gettimeofday(&s_now, NULL);
	return s_now;
}

double
_timeDiff(struct timeval a, struct timeval b)
{
	double time1;
	double time2;
	time1 = a.tv_sec + a.tv_usec / (double)1000000;
	time2 = b.tv_sec + b.tv_usec / (double)1000000;
	return time1 - time2;
}
