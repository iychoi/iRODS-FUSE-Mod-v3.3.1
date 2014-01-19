/* bgdownload.h */

#ifndef BGDOWNLOAD_H
#define BGDOWNLOAD_H

#ifdef  __cplusplus
extern "C" {
#endif

#define MAX_BG_THREADS  5
#define BGDOWNLOAD_CACHE_PATH "/tmp/irods_bgdncache"

int startBgDownload(const char *path, int flags);
int _download(const char *path, int flags);

int updateFileTime(const char *inPath, struct stat *stbuf);

int checkCacheExistance(const char *inPath, struct stat *stbuf);

int invalidateCacheFile(const char *inPath);

int getDownloadCachePath(const char *inPath, char *cachePath);

int makeDownloadCacheDir(const char *filePath);

int isDirectory(const char *path);

int makeDirs(const char *path);

#ifdef  __cplusplus
}
#endif

#endif	/* BGDOWNLOAD_H */
