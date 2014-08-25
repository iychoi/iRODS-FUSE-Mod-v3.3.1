/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/* irodsFs.c - The main program of the iRODS/Fuse server. It is to be run to
 * serve a single client 
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include "irodsFs.h"
#include "iFuseOper.h"
#include "iFuseLib.h"

/* some global variables */

extern rodsEnv MyRodsEnv;
fuseConfig_t MyFuseConfig;
#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
preloadConfig_t MyPreloadConfig;
lazyUploadConfig_t MyLazyUploadConfig;
#endif

#ifdef  __cplusplus
struct fuse_operations irodsOper; 
#else
static struct fuse_operations irodsOper =
{
  .getattr = irodsGetattr,
  .readlink = irodsReadlink,
  .readdir = irodsReaddir,
  .mknod = irodsMknod,
  .mkdir = irodsMkdir,
  .symlink = irodsSymlink,
  .unlink = irodsUnlink,
  .rmdir = irodsRmdir,
  .rename = irodsRename,
  .link = irodsLink,
  .chmod = irodsChmod,
  .chown = irodsChown,
  .truncate = irodsTruncate,
  .utimens = irodsUtimens,
  .open = irodsOpen,
  .read = irodsRead,
  .write = irodsWrite,
  .statfs = irodsStatfs,
  .release = irodsRelease,
  .fsync = irodsFsync,
  .flush = irodsFlush,
};
#endif

int parseFuseCmdLineOpt (int argc, char **argv, fuseConfig_t *fuseConfig);
#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
int parsePreloadAndLazyUploadCmdLineOpt (int argc, char **argv, preloadConfig_t *preloadConfig, lazyUploadConfig_t *lazyUploadConfig);
int releasePreloadConfig (preloadConfig_t *preloadConfig);
int releaseLazyUploadConfig (lazyUploadConfig_t *lazyUploadConfig);
int makeCleanCmdLineOpt (int argc, char **argv, int *argc2, char ***argv2);
int releaseUserCreatedCmdLineOpt (int argc, char **argv);
#endif

void usage ();

/* Note - fuse_main parses command line options 
 * static const struct fuse_opt fuse_helper_opts[] = {
    FUSE_HELPER_OPT("-d",          foreground),
    FUSE_HELPER_OPT("debug",       foreground),
    FUSE_HELPER_OPT("-f",          foreground),
    FUSE_HELPER_OPT("-s",          singlethread),
    FUSE_HELPER_OPT("fsname=",     nodefault_subtype),
    FUSE_HELPER_OPT("subtype=",    nodefault_subtype),

    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-ho",         KEY_HELP_NOHEADER),
    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    FUSE_OPT_KEY("-d",          FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("debug",       FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("fsname=",     FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("subtype=",    FUSE_OPT_KEY_KEEP),
    FUSE_OPT_END
};

static void usage(const char *progname)
{
    fprintf(stderr,
            "usage: %s mountpoint [options]\n\n", progname);
    fprintf(stderr,
            "general options:\n"
            "    -o opt,[opt...]        mount options\n"
            "    -h   --help            print help\n"
            "    -V   --version         print version\n"
            "\n");
}

*/

int 
main (int argc, char **argv)
{

irodsOper.getattr = irodsGetattr;
irodsOper.readlink = irodsReadlink;
irodsOper.readdir = irodsReaddir;
irodsOper.mknod = irodsMknod;
irodsOper.mkdir = irodsMkdir;
irodsOper.symlink = irodsSymlink;
irodsOper.unlink = irodsUnlink;
irodsOper.rmdir = irodsRmdir;
irodsOper.rename = irodsRename;
irodsOper.link = irodsLink;
irodsOper.chmod = irodsChmod;
irodsOper.chown = irodsChown;
irodsOper.truncate = irodsTruncate;
irodsOper.utimens = irodsUtimens;
irodsOper.open = irodsOpen;
irodsOper.read = irodsRead;
irodsOper.write = irodsWrite;
irodsOper.statfs = irodsStatfs;
irodsOper.release = irodsRelease;
irodsOper.fsync = irodsFsync;
irodsOper.flush = irodsFlush;


    int status;
    rodsArguments_t myRodsArgs;
    char *optStr;
    
    int argc2;
    char** argv2;

#ifdef  __cplusplus
    bzero (&irodsOper, sizeof (irodsOper));
    irodsOper.getattr = irodsGetattr;
    irodsOper.readlink = irodsReadlink;
    irodsOper.readdir = irodsReaddir;
    irodsOper.mknod = irodsMknod;
    irodsOper.mkdir = irodsMkdir;
    irodsOper.symlink = irodsSymlink;
    irodsOper.unlink = irodsUnlink;
    irodsOper.rmdir = irodsRmdir;
    irodsOper.rename = irodsRename;
    irodsOper.link = irodsLink;
    irodsOper.chmod = irodsChmod;
    irodsOper.chown = irodsChown;
    irodsOper.truncate = irodsTruncate;
    irodsOper.utimens = irodsUtimens;
    irodsOper.open = irodsOpen;
    irodsOper.read = irodsRead;
    irodsOper.write = irodsWrite;
    irodsOper.statfs = irodsStatfs;
    irodsOper.release = irodsRelease;
    irodsOper.fsync = irodsFsync;
    irodsOper.flush = irodsFlush;
#endif

    status = getRodsEnv (&MyRodsEnv);

    if (status < 0) {
        rodsLogError(LOG_ERROR, status, "main: getRodsEnv error. ");
        exit (1);
    }

    /* parse the fuse command-line options */
    status = parseFuseCmdLineOpt (argc, argv, &MyFuseConfig);
    
    if (status < 0) {
        printf("Use -h for help.\n");
        exit (1);
    }

    if (MyFuseConfig.nonempty) {
        rodsLog (LOG_DEBUG, "fuse nonempty option is set");
    }
    if (MyFuseConfig.foreground) {
        rodsLog (LOG_DEBUG, "fuse foreground option is set");
    }
    if (MyFuseConfig.debug) {
        rodsLog (LOG_DEBUG, "fuse debug option is set");
    }

#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
    /* handle the preload and lazy upload command line options*/
    status = parsePreloadAndLazyUploadCmdLineOpt (argc, argv, &MyPreloadConfig, &MyLazyUploadConfig);

    if (status < 0) {
        printf("Use -h for help.\n");
        exit (1);
    }

    status = makeCleanCmdLineOpt (argc, argv, &argc2, &argv2);

    argc = argc2;
    argv = argv2;
#endif

    optStr = "hdo:";

    status = parseCmdLineOpt (argc, argv, optStr, 0, &myRodsArgs);    

    if (status < 0) {
        printf("Use -h for help.\n");
        exit (1);
    }
    if (myRodsArgs.help==True) {
       usage();
       exit(0);
    }

    srandom((unsigned int) time(0) % getpid());

#ifdef CACHE_FILE_FOR_READ
    if (setAndMkFileCacheDir () < 0) exit (1);
#endif

    initPathCache ();
    initIFuseDesc ();
    initConn();
    initFileCache();

#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
    // initialize preload
    initPreload (&MyPreloadConfig, &MyRodsEnv, &myRodsArgs);
    initLazyUpload (&MyLazyUploadConfig, &MyRodsEnv, &myRodsArgs);
#endif
    {
        for(int i=0;i<argc;i++) {
            printf("argv[%d] = %s\n", i, argv[i]);
        }
    }

    status = fuse_main (argc, argv, &irodsOper, NULL);

#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
    /* release the preload command line options */
    releaseUserCreatedCmdLineOpt (argc, argv);

    // wait preload & lazy upload jobs
    waitLazyUploadJobs();
    waitPreloadJobs();

    // uninitialize preload
    uninitPreload (&MyPreloadConfig);
    releasePreloadConfig (&MyPreloadConfig);

    // uninitialize lazy upload
    uninitLazyUpload (&MyLazyUploadConfig);
    releaseLazyUploadConfig (&MyLazyUploadConfig);
#endif

    disconnectAll ();

    if (status < 0) {
        exit (3);
    } else {
        exit(0);
    }
}

int
parseFuseCmdLineOpt (int argc, char **argv, fuseConfig_t *fuseConfig) {
    int i;
    memset(fuseConfig, 0, sizeof(fuseConfig_t));

    for (i=0;i<argc;i++) {
        if (strcmp("-o", argv[i])==0) {
            if (i + 2 < argc) {
                if (strcmp("nonempty", argv[i+1])==0) {
                    fuseConfig->nonempty = 1;
                    // do not pass nonempty to fuse -- this will cause crash
                    argv[i]="-Z";
                    argv[i+1]="-Z";
                }
                if (strcmp("-f", argv[i+1])==0) {
                    fuseConfig->foreground = 1;
                    //argv[i]="-Z";
                    //argv[i+1]="-Z";
                }
                if (strcmp("-d", argv[i+1])==0) {
                    fuseConfig->debug = 1;
                    //argv[i]="-Z";
                    //argv[i+1]="-Z";
                }
            }
        }
    }

    return(0);
}

#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
int 
parsePreloadAndLazyUploadCmdLineOpt (int argc, char **argv, preloadConfig_t *preloadConfig, lazyUploadConfig_t *lazyUploadConfig) {
    int i;
    memset(preloadConfig, 0, sizeof(preloadConfig_t));
    memset(lazyUploadConfig, 0, sizeof(lazyUploadConfig_t));

    for (i=0;i<argc;i++) {
        if (strcmp("--preload", argv[i])==0) {
            preloadConfig->preload=True;
            argv[i]="-Z";
        }
        if (strcmp("--preload-clear-cache", argv[i])==0) {
            preloadConfig->preload=True;
            preloadConfig->clearCache=True;
            argv[i]="-Z";
        }
        if (strcmp("--preload-cache-dir", argv[i])==0) {
            argv[i]="-Z";
            if (i + 2 < argc) {
                if (*argv[i+1] == '-') {
                    rodsLog (LOG_ERROR,
                    "--preload-cache-dir option takes a directory argument");
                    return USER_INPUT_OPTION_ERR;
                }
                preloadConfig->preload=True;
                preloadConfig->cachePath=strdup(argv[i+1]);
                argv[i+1]="-Z";
            }
        }
        if (strcmp("--preload-cache-max", argv[i])==0) {
            argv[i]="-Z";
            if (i + 2 < argc) {
                if (*argv[i+1] == '-') {
                    rodsLog (LOG_ERROR,
                    "--preload-cache-max option takes a size argument");
                    return USER_INPUT_OPTION_ERR;
                }
                preloadConfig->preload=True;
                preloadConfig->cacheMaxSize=strtoll(argv[i+1], 0, 0);
                argv[i+1]="-Z";
            }
        }
        if (strcmp("--preload-file-min", argv[i])==0) {
            argv[i]="-Z";
            if (i + 2 < argc) {
                if (*argv[i+1] == '-') {
                    rodsLog (LOG_ERROR,
                    "--preload-file-min option takes a size argument");
                    return USER_INPUT_OPTION_ERR;
                }
                preloadConfig->preload=True;
                preloadConfig->preloadMinSize=strtoll(argv[i+1], 0, 0);
                argv[i+1]="-Z";
            }
        }
        if (strcmp("--lazyupload", argv[i])==0) {
            lazyUploadConfig->lazyUpload=True;
            argv[i]="-Z";
        }
        if (strcmp("--lazyupload-buffer-dir", argv[i])==0) {
            argv[i]="-Z";
            if (i + 2 < argc) {
                if (*argv[i+1] == '-') {
                    rodsLog (LOG_ERROR,
                    "--lazyupload-cache-dir option takes a directory argument");
                    return USER_INPUT_OPTION_ERR;
                }
                lazyUploadConfig->lazyUpload=True;
                lazyUploadConfig->bufferPath=strdup(argv[i+1]);
                argv[i+1]="-Z";
            }
        }
    }

    // set default
    if(preloadConfig->cachePath == NULL) {
        rodsLog (LOG_DEBUG, "parsePreloadAndLazyUploadCmdLineOpt: uses default preload cache dir - %s", FUSE_PRELOAD_CACHE_DIR);
        preloadConfig->cachePath=strdup(FUSE_PRELOAD_CACHE_DIR);
    }

    if(preloadConfig->preloadMinSize < MAX_READ_CACHE_SIZE) {
        // in this case, given iRODS file is not cached by preload but cached by file cache.
        rodsLog (LOG_DEBUG, "parsePreloadAndLazyUploadCmdLineOpt: uses default min size %lld - (given %lld)", MAX_READ_CACHE_SIZE, preloadConfig->preloadMinSize);
        preloadConfig->preloadMinSize = MAX_READ_CACHE_SIZE;
    }

    if(lazyUploadConfig->bufferPath == NULL) {
        rodsLog (LOG_DEBUG, "parsePreloadAndLazyUploadCmdLineOpt: uses default lazy-upload buffer dir - %s", FUSE_LAZY_UPLOAD_BUFFER_DIR);
        lazyUploadConfig->bufferPath=strdup(FUSE_LAZY_UPLOAD_BUFFER_DIR);
    }

    return(0);
}

int
releasePreloadConfig (preloadConfig_t *preloadConfig) {
    if (preloadConfig==NULL) {
        return -1;
    }

    if (preloadConfig->cachePath!=NULL) {
        free(preloadConfig->cachePath);
    }

    return(0);
}

int
releaseLazyUploadConfig (lazyUploadConfig_t *lazyUploadConfig) {
    if (lazyUploadConfig==NULL) {
        return -1;
    }

    if (lazyUploadConfig->bufferPath!=NULL) {
        free(lazyUploadConfig->bufferPath);
    }

    return(0);
}

int
makeCleanCmdLineOpt (int argc, char **argv, int *argc2, char ***argv2) {
    int i;
    int actual_argc = 0;
    int j;

    if (argc2==NULL) {
        return -1;
    }
    
    if (argv2==NULL) {
        return -1;
    }

    for (i=0;i<argc;i++) {
        if (strcmp("-Z", argv[i])!=0) {
            actual_argc++;
        }
    }

    *argv2=(char**)malloc(sizeof(char*) * actual_argc);

    j=0;
    for (i=0;i<argc;i++) {
        if (strcmp("-Z", argv[i])!=0) {
            (*argv2)[j]=strdup(argv[i]);
            j++;
        }
    }

    *argc2=actual_argc;
    return(0);
}

int
releaseUserCreatedCmdLineOpt (int argc, char **argv) {
    int i;
    
    if (argv!=NULL) {
        for (i=0;i<argc;i++) {
            if (argv[i]!=NULL) {
                free(argv[i]);
            }
        }
        free(argv);
    }
    
    return(0);
}
#endif

void
usage ()
{
   char *msgs[]={
#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
   "Usage : irodsFs [-hd] [--preload] [--preload-clear-cache] [--preload-cache-dir dir] [--preload-cache-max maxsize] [--preload-file-min minsize] [--lazyupload] [--lazyupload-buffer-dir dir] [-o opt,[opt...]]",
#else
   "Usage : irodsFs [-hd] [-o opt,[opt...]]",
#endif
"Single user iRODS/Fuse server",
"Options are:",
" -h  this help",
" -d  FUSE debug mode",
" -o  opt,[opt...]  FUSE mount options",

#ifdef ENABLE_PRELOAD_AND_LAZY_UPLOAD
" --preload                use preload",
" --preload-clear-cache    clear preload caches",
" --preload-cache-dir      specify preload cache directory",
" --preload-cache-max      specify preload cache max limit (in bytes)", 
" --preload-file-min       specify minimum file size that will be preloaded (in bytes)",
" --lazyupload             use lazy-upload",
" --lazyupload-buffer-dir  specify lazy-upload buffer directory",
#endif

""};
    int i;
    for (i=0;;i++) {
        if (strlen(msgs[i])==0) return;
         printf("%s\n",msgs[i]);
    }
}


