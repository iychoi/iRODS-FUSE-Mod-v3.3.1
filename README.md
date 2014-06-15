iRODS-FUSE-Mod
==============

Modified version of iRODS-FUSE client in order to optimize file read performance

Overview
--------

Original read performance of iRODS FUSE client is way slower than "iget" command-line utility. This is because "iget" uses multi-threaded parallel access to the data file and bigger chunk size per request while "iRODS FUSE client" uses single thread and small chunk size. Hence "iget" program utilizes network bandwidth more efficiently.

In this project, read operations will be boosted by the same manner with "iget". While reading a big file from iRODS server, iRODS FUSE will download the whole file to local disk. Later read operations will be the same as simple local file read.

FUSE Configuration Options
--------------------------

- "--preload" : use preload
- "--preload-cache-dir" : specify preload cache directory, if not specified, "/tmp/fusePreloadCache/" will be used
- "--preload-cache-max" : specify preload cache max limit (in bytes)

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
