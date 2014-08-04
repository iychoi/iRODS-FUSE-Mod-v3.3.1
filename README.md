iRODS-FUSE-Mod
==============

iRODS-FUSE-Mod is a modified version of iRODS-FUSE (irodsFs) to provide preload and lazy-upload for better performance.

Overview
--------

Original read performance of iRODS FUSE (irodsFs) is slower than "iget" and "iput" command-line tools when we try to read and write large data files stored on remote iRODS servers. This is because "iget" and "iput" uses multi-threaded parallel accesses to remote data files and uses bigger chunk size per request while iRODS FUSE (irodsFs) uses a single thread and small chunk size.

In this modification, file read and write operations are improved by using the same techniques as "iget" and "iput". While reading a big remote file, the modified iRODS FUSE will download the whole file to local disk in a background. Hence, subsequent file read will be boosted. While writing a big file, the modified iRODS FUSE will store the file content written to the local disk and upload the content in the future when the file is closed.

FUSE Configuration Options
--------------------------

- "--preload" : use preload
- "--preload-clear-cache" : clear preload caches
- "--preload-cache-dir" : specify preload cache directory, if not specified, "/tmp/fusePreloadCache/" will be used
- "--preload-cache-max" : specify preload cache max limit (in bytes)
- "--preload-file-min" : specify minimum file size that will be preloaded (in bytes)

- "--lazyupload" : use lazy-upload
- "--lazyupload-buffer-dir" : specify lazy-upload buffer directory, if not specified, "/tmp/fuseLazyUploadBuffer/" will be used

If you just want to use the preloading without configuring other parameters that relate to the preloading feature, you will need to give "--preload" option. If you use any other options that relate to the preloading, you don't need to give "--preload". Those options will also set "--preload" option by default.

If you just want to use the lazy-uploading without configuring other parameters that relate to the lazy-uploading feature, you will need to give "--lazyupload" option. If you use any other options that relate to the lazy-uploading, you don't need to give "--lazyupload". Those options will also set "--lazyupload" option by default.

Performance Metrics
-------------------

Tested with iPlant Atmosphere virtual instance and iPlant DataStore(iRODS).

File Read Performance

File Size | iRODS-FUSE (Unmodified) | iRODS-FUSE-Mod
--- | --- | ---
10MB | 0.7 seconds | 0.3 seconds
50MB | 1.7 seconds | 1.3 seconds
100MB | 3.3 seconds | 2.1 seconds
500MB | 17 seconds | 7.4 seconds
1GB | 34.7 seconds | 14.1 seconds
2GB | 71.0 seconds | 34.5 seconds

File Write Performance

- to be filled


Debug Mode
--------------

To see debug messages that iRODS FUSE (irodsFs) prints out, edit "~/.irods/.irodsEnv" file and add "irodsLogLevel" parameter. 1 means "nothing" and 9 means "many".
