#ifndef I_FUSE_LIB_LAZYUPLOAD_H
#define I_FUSE_LIB_LAZYUPLOAD_H

#include "rodsClient.h"
#include "rodsPath.h"
#include "iFuseLib.h"
#include "iFuseLib.Lock.h"

#define FUSE_LAZY_UPLOAD_BUFFER_DIR  "/tmp/fuseLazyUploadBuffer"

#ifdef USE_BOOST
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#endif

typedef struct LazyUploadConfig {
    int lazyUpload;
    char *bufferPath;
} lazyUploadConfig_t;

#define NUM_LAZYUPLOAD_THREAD_HASH_SLOT	201
#define NUM_LAZYUPLOAD_FILE_HASH_SLOT   201

typedef struct LazyUploadThreadInfo {
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
} lazyUploadThreadInfo_t;

#define LAZYUPLOAD_THREAD_RUNNING    1
#define LAZYUPLOAD_THREAD_IDLE    0

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

typedef struct LazyUploadThreadData {
    char *path;
    lazyUploadThreadInfo_t *threadInfo;
} lazyUploadThreadData_t;

#ifdef  __cplusplus
extern "C" {
#endif

// lazy-upload
int
initLazyUpload (lazyUploadConfig_t *lazyUploadConfig, rodsEnv *myRodsEnv, rodsArguments_t *myRodsArgs);
int
waitLazyUploadJobs ();
int
uninitLazyUpload (lazyUploadConfig_t *lazyUploadConfig);
int
isLazyUploadEnabled ();
int
isFileBufferedForLazyUpload (const char *path);
int
isBufferedFileUploading (const char *path);
int
mknodLazyUploadBufferedFile (const char *path);
int
openLazyUploadBufferedFile (const char *path, int accmode);
int
writeLazyUploadBufferedFile (const char *path, const char *buf, size_t size, off_t offset);
int
closeLazyUploadBufferedFile (const char *path);
#ifdef  __cplusplus
}
#endif

#endif	/* I_FUSE_LIB_LAZYUPLOAD_H */
