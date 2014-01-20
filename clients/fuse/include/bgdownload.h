/* bgdownload.h */

#ifndef BGDOWNLOAD_H
#define BGDOWNLOAD_H

#ifdef  __cplusplus
extern "C" {
#endif

#define BGDOWNLOAD_CACHE_PATH "/tmp/irods_bgdncache"

#define MAX_BG_THREADS  5

#define DOWNLOAD_THREAD_RUNNING    1
#define DOWNLOAD_THREAD_IDLE    0

int initializeBgDownload();

int startBgDownload(const char *path, int flags);
int _download(const char *path, int flags);

int completeFileDownload(const char *inPath, const char *destPath, struct stat *stbuf);

int checkCacheExistanceNoCheck(const char *inPath);

int checkCacheExistance(const char *inPath, struct stat *stbuf);

int invalidateCacheFile(const char *inPath);

int getDownloadCachePath(const char *inPath, char *cachePath);

int getTempDownloadCachePath(const char *inPath, char *cachePath);

int makeDownloadCacheDir(const char *filePath);

int isDirectory(const char *path);

int makeDirs(const char *path);

#ifdef  __cplusplus
}
#endif

#endif	/* BGDOWNLOAD_H */
