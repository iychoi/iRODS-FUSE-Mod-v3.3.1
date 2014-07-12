iRODS-FUSE-Mod
==============

Modified version of iRODS-FUSE client that supports file preload and lazy upload to improve I/O performance

Overview
--------

Original read performance of iRODS FUSE client is slower than "iget" and "iput" command-line tools. This is because "iget" and "iput" uses multi-threaded parallel access to remote data files and uses bigger chunk size per request while "iRODS FUSE client" uses single thread and small chunk size. Hence "iget" program utilizes network bandwidth more efficiently.

In this project, file operations will be improved by using the same techniques as "iget" and "iput". While reading a big remote file, the modified iRODS FUSE will download the whole file to local disk in background. Hence, subsequent read operations will be benefitted by local cache. While writing a big file, the modified iRODS FUSE will save the file content to local disk first and commit (upload) the content when the file handle is closed.

FUSE Configuration Options
--------------------------

- "--preload" : use preload
- "--preload-cache-dir" : specify preload cache directory, if not specified, "/tmp/fusePreloadCache/" will be used
- "--preload-cache-max" : specify preload cache max limit (in bytes)
- "--preload-file-min" : specify minimum file size that will be preloaded (in bytes)

If you just want to use the preloading without configuring other parameters, you will need to give "--preload" option. If you use any other options that relate to the preloading, you don't need to give "--preload". Those options will also set "--preload" option by default.

Performance Metrics
-------------------

Tested with iPlant Atmosphere virtual instance and iPlant DataStore(iRODS).

File Size | iRODS-FUSE (Unmodified) | iRODS-FUSE-Mod
--- | --- | ---
10MB | 0.7 seconds | 0.3 seconds
50MB | 1.7 seconds | 1.3 seconds
100MB | 3.3 seconds | 2.1 seconds
500MB | 17 seconds | 7.4 seconds
1GB | 34.7 seconds | 14.1 seconds
2GB | 71.0 seconds | 34.5 seconds
