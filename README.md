iRODS-FUSE-Mod
==============

Modified version of iRODS-FUSE client in order to optimize file read performance

Optimization
------------

Original read performance of iRODS FUSE client is way slower than "iget" command-line utility. This is because "iget" uses multi-threaded parallel access to the data file and bigger chunk size per request while "iRODS FUSE client" uses single thread and small chunk size. Hence "iget" program utilizes network bandwidth more efficiently.

In this project, read operations will be optimized by fusing "iRODS FUSE client" and "iget" program. While reading a big file from iRODS server by using traditional way, this will download whole file to local disk by using the way "iget" used in the background simultenuously.

