/* bgdownload.h */

#ifndef BGDOWNLOAD_H
#define BGDOWNLOAD_H

#ifdef  __cplusplus
extern "C" {
#endif

#define _DEBUG

#ifdef _DEBUG
#define _DBG_(x)	x
#else
#define _DBG_(x)
#endif

#define bgdn_log(fmt, args...) {										\
	_DBG_(																\
		printf("%s:%d: " fmt, __FILE__, __LINE__, ##args);				\
	)																	\
}

#define BGDOWNLOAD_CACHE_PARENT_PATH    "/tmp"
#define BGDOWNLOAD_CACHE_PATH "/tmp/irods_bgdncache"

#define MAX_BG_THREADS  5

#define DOWNLOAD_THREAD_RUNNING    1
#define DOWNLOAD_THREAD_IDLE    0

// public functions
int bgdnInitialize();
int bgdnUninitialize();

int bgdnDownload(const char *path, int flags);
int bgdnHasCache(const char *inPath);
int bgdnGetCachePath(const char *inPath, char *cachePath);

#ifdef  __cplusplus
}
#endif

#endif	/* BGDOWNLOAD_H */
