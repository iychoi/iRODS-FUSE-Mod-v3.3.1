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
#include "putUtil.h"

/**************************************************************************
 * global variables
 **************************************************************************/
static lazyUploadConfig_t LazyUploadConfig;
static rodsEnv *LazyUploadRodsEnv;
static rodsArguments_t *LazyUploadRodsArgs;

/**************************************************************************
 * function definitions
 **************************************************************************/
static int _prepareLazyUploadBufferDir(const char *path);
static int _getiRODSPath(const char *path, char *iRODSPath);
static int _getBufferPath(const char *path, char *bufferPath);
static int _hasBufferredFile(const char *path);
static int _removeAllBufferredFiles();
static int _removeBufferredFile(const char *path);
static int _prepareDir(const char *path);
static int _isDirectory(const char *path);
static int _makeDirs(const char *path);
static int _emptyDir(const char *path);
static int _removeDir(const char *path);
static struct timeval _getCurrentTime();
static time_t _convTime(struct timeval);

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

    _prepareLazyUploadBufferDir(lazyUploadConfig->bufferPath);

    // remove incomplete lazy-upload bufferred files
    _removeAllBufferredFiles();

    return (0);
}

int
uninitLazyUpload (lazyUploadConfig_t *lazyUploadConfig) {
    // remove incomplete lazy-upload caches
    _removeAllBufferredFiles();

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
    char iRODSPath[MAX_NAME_LEN];
    char bufferPath[MAX_NAME_LEN];

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "prepareLazyUploadBufferredFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "prepareLazyUploadBufferredFile: failed to get iRODS path - %s", path);
        return status;
    }

    status = _getBufferPath(path, bufferPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "prepareLazyUploadBufferredFile: _getBufferPath error.");
        rodsLog (LOG_ERROR, "prepareLazyUploadBufferredFile: failed to get buffer path - %s", path);
        return status;
    }

    // make dir
    _prepareDir(bufferPath);

    rodsLog (LOG_DEBUG, "prepareLazyUploadBufferredFile: create a bufferred file - %s", iRODSPath);    
    int desc = open (bufferPath, O_RDWR|O_CREAT);
    close (desc);

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

    // convert input path to iRODSPath
    status = _getiRODSPath(path, iRODSPath);
    if(status < 0) {
        rodsLogError(LOG_ERROR, status, "uploadFile: _getiRODSPath error.");
        rodsLog (LOG_ERROR, "uploadFile: failed to get iRODS path - %s", path);
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
    rstrcpy( rodsPathInp.destPath->inPath, iRODSPath, MAX_NAME_LEN );
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

    //TODO: upload bufferred file here
    rodsLog (LOG_DEBUG, "uploadFile: upload %s", iRODSPath);
    status = putUtil (&conn, LazyUploadRodsEnv, LazyUploadRodsArgs, &rodsPathInp);
    rodsLog (LOG_DEBUG, "uploadFile: complete uploading %s", iRODSPath);

    // clear buffer
    _removeBufferredFile(iRODSPath);

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
        return status;
    }

    if ((status = stat(bufferPath, &stbufCache)) < 0) {
        return status;
    }

    return (0);
}

static int
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
        rstrcpy (iRODSPath, LazyUploadRodsEnv->rodsCwd, MAX_NAME_LEN);
        return (0);
    } else if (strcmp (path, ".") == 0 || strcmp (path, "./") == 0) {
    	/* '.' or './' */
	    rstrcpy (iRODSPath, LazyUploadRodsEnv->rodsCwd, MAX_NAME_LEN);
    	return (0);
    } else if (strcmp (path, "~") == 0 || strcmp (path, "~/") == 0 || strcmp (path, "^") == 0 || strcmp (path, "^/") == 0) { 
    	/* ~ or ~/ */
        rstrcpy (iRODSPath, LazyUploadRodsEnv->rodsHome, MAX_NAME_LEN);
        return (0);
    } else if (path[0] == '~' || path[0] == '^') {
	    if (path[1] == '/') {
	        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", LazyUploadRodsEnv->rodsHome, path + 2);
	    } else {
	        /* treat it like a relative path */
	        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", LazyUploadRodsEnv->rodsCwd, path + 2);
	    }
    } else if (path[0] == '/') {
	    /* full path */
        //rstrcpy (iRODSPath, (char*)path, MAX_NAME_LEN);
        snprintf (iRODSPath, MAX_NAME_LEN, "%s%s", LazyUploadRodsEnv->rodsCwd, path);
    } else {
	    /* a relative path */
        snprintf (iRODSPath, MAX_NAME_LEN, "%s/%s", LazyUploadRodsEnv->rodsCwd, path);
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

    status = _makeDirs(path);

    return status; 
}

static int
_prepareDir(const char *path) {
    int status;
    char dir[MAX_NAME_LEN], file[MAX_NAME_LEN];
    
    if (path == NULL) {
        rodsLog (LOG_ERROR, "_prepareDir: input path is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    splitPathByKey ((char *) path, dir, file, '/');
    
    // make dirs
    status = _makeDirs(dir);

    return status;
}

static int
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

static int
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

static int
_removeAllBufferredFiles() {
    int status;
    
    if((status = _emptyDir(LazyUploadConfig.bufferPath)) < 0) {
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
_emptyDir(const char *path) {
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
            rodsLog (LOG_DEBUG, "_emptyDir: removing : %s", dir);

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

    return statusFailed;
}

static int
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

static struct timeval
_getCurrentTime() {
	struct timeval s_now;
	gettimeofday(&s_now, NULL);
	return s_now;
}

static time_t
_convTime(struct timeval a) {
    return (time_t)a.tv_sec;
}
