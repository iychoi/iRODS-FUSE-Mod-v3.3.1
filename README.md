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

