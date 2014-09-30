#!/bin/bash

#  Simple script to create various files for testing FUSE module
#
#  Created: 2014-08-07  by Jerry Schneider

# set STARTTIME to the current time
STARTTIMESECS=`date +%s`
STARTTIME=`date +%c`


LocalTestDir=$HOME/test_downloads
#FuseMountPoint=$HOME/fuse_mount
FuseMountPoint=/mnt/fuse_mount
LOGFILE=$HOME/CreateFUSETestFiles.log
#iRODSUserName=ipctest
#iRODSUserPass=ipctest

#  iRODS file locations and names
iRODSBaseDir=$HOME/trunk/iRODS-FUSE-Mod
#iRODSBaseDir=/usr/local/bin
iRODSFUSEbin=$iRODSBaseDir/clients/fuse/bin/irodsFs
#iRODSFUSEbin=$iRODSBaseDir/irodsFs
#iRODSClientBin=$iRODSBaseDir/clients/icommands/bin
#iRODSClientBin=$iRODSBaseDir

# Parameter list to hand off to irodsFs (FUSE)
#FUSEParams="-o nonempty -o -f --preload --lazyupload"
#FUSEParams="-o nonempty --preload --lazyupload"
#FUSEParams="--preload --lazyupload"
FUSEParams="--preload-cache-dir /vol1/fuse/fusePreloadCache/ --lazyupload-buffer-dir /vol1/fuse/fuseLazyUploadBuffer/"
#FUSEParams="-o max_readahead=0"
#FUSEParams="-o nonempty"

# List of filenames to work with individually
#FileNameList="4.5MBFile.junk 45MBFile.junk 450MBFile.junk"
FileNameList="4.5MBFile.junk 45MBFile.junk 450MBFile.junk 1.9GBFile.junk"


# Delete old logfile
rm -rf $LOGFILE


#  Make sure the directories needed for the script exist.  If not, create them.
echo  "----------------------------------------------------------------------" > $LOGFILE
echo  "- - -  Verify directories exist and ready" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Verify directories exist and ready"
echo  "----------------------------------------------------------------------"
if [ ! -d "$LocalTestDir" ]; then
    mkdir $LocalTestDir >> $LOGFILE 2>&1
else
	rm -rf $LocalTestDir/* >> $LOGFILE 2>&1
fi
#  Unmount FUSE mount to be used for testing if already mounted
cd  >> $LOGFILE 2>&1 
if [ ! -d "$FuseMountPoint" ]; then
	mkdir $FuseMountPoint >> $LOGFILE 2>&1
else
    FuseMaxCount=0
	FuseMounted=
	FuseMounted=$(df -h | grep irodsFs)
	#lsof $HOME/fuse_mount
	if [ ! "$FuseMounted" == "" ]; then
		fusermount -u $FuseMountPoint >> $LOGFILE 2>&1
		FMExitCode=$?
        until [[ "$FMExitCode" = "0" || "$FuseMaxCount" > "60" ]]; do
                fusermount -u $FuseMountPoint
                FMExitCode=$?
                sleep 5
                let FuseMaxCount=FuseMaxCount+5
                echo "FuseMaxCount is [$FuseMaxCount]"
        done
		echo "- - -   FUSE mount point unmounted now"
	else
		echo "- - -  Fuse mount point is NOT mounted" >> $LOGFILE
		echo "- - -  Fuse mount point is NOT mounted"
		rm -rf $FuseMountPoint/* >> $LOGFILE 2>&1
	fi
fi


#  Now create files to work with on the local filesystem
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Creating files in $LocalTestDir/CreateFiles/" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Creating files in $LocalTestDir/CreateFiles/"
echo  "----------------------------------------------------------------------"
cd $LocalTestDir >> $LOGFILE 2>&1
if [ ! -d "$LocalTestDir/CreateFiles" ]; then
	mkdir -p $LocalTestDir/CreateFiles >> $LOGFILE 2>&1
else
	rm -rf $LocalTestDir/CreateFiles/* >> $LOGFILE 2>&1
fi
cd $LocalTestDir/CreateFiles >> $LOGFILE 2>&1


#  Begin actually creating files to work with in the local filesystem
echo  "- - -  Creating 4.5MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=4.5MBFile.junk bs=1024 iflag=fullblock count=4608 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 4.5MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	#echo "ddExitCode is [$ddExitCode]"
	echo "* * *   ERROR  -  There was an error creating 4.5MBFile.junk"
fi

echo  "- - -  Creating 45MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=45MBFile.junk bs=1024 iflag=fullblock count=46080 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 45MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 45MBFile.junk"
fi

echo  "- - -  Creating 450MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=450MBFile.junk bs=1024 iflag=fullblock count=460800 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 450MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 450MBFile.junk"
fi

echo  "- - -  Creating 1.9GB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=1.9GBFile.junk bs=1024 iflag=fullblock count=2046000 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 1.9GBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 1.9GBFile.junk"
fi

#echo  "- - -  Creating 4.0GB file"
#dd if=/dev/zero of=4.0GBFile.junk bs=1024 iflag=fullblock count=4096000 >> $LOGFILE 2>&1

#echo  "- - -  Creating 4.5GB file"
#dd if=/dev/zero of=4.5GBFile.junk bs=1024 iflag=fullblock count=4608000 >> $LOGFILE 2>&1

#echo  "- - -  Creating 4.6GB file"
#dd if=/dev/zero of=4.6GBFile.junk bs=1024 iflag=fullblock count=4736000 >> $LOGFILE 2>&1

#echo  "- - -  Creating 5.0GB file"
#dd if=/dev/zero of=5.0GBFile.junk bs=1024 iflag=fullblock count=5120000 >> $LOGFILE 2>&1


#  Create sha1sums for files created
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Create sha1sum for each file created above" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Create sha1sum for each file created above"
echo  "----------------------------------------------------------------------"
cd $LocalTestDir/CreateFiles >> $LOGFILE 2>&1
for FileName in $FileNameList
do
	echo  "- - -  Creating sha1 for $FileName"
	CopyStartTime=`date +%s`
	sha1sum $FileName > $FileName.sha1
	Sha1ExitCode=$?
	CopyEndTime=`date +%s`
	echo --  Total time to create sha1sum for $FileName file was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
	if [ ! "$Sha1ExitCode" == "0" ]; then
		echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
		echo "* * *   ERROR  -  Sha1sum file not created correctly for $Filename"
	fi
done


#  Now copy created files to test_downloads_SingleCopyDir for timing comparison of
#  local filesystem copies
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now copy individual files from CreateFiles to SingleCopyDir" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now copy individual files from CreateFiles to SingleCopyDir"
echo  "----------------------------------------------------------------------"

cd $FuseMountPoint >> $LOGFILE 2>&1
if [ ! -d "$LocalTestDir/SingleCopyDir" ]; then
	mkdir -p $LocalTestDir/SingleCopyDir >> $LOGFILE 2>&1
else
	rm -rf $LocalTestDir/SingleCopyDir/* >> $LOGFILE 2>&1
fi

for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now copying $FileName from local to local" >> $LOGFILE
	echo  "- - -  Now copying $FileName from local to local"
	cp $LocalTestDir/CreateFiles/$FileName $LocalTestDir/SingleCopyDir/ >> $LOGFILE 2>&1
	CopyEndTime=`date +%s`
	echo --  Total time for copy of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now verify individual file sha1sum to original file" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now verify individual file sha1sum to original file"
echo  "----------------------------------------------------------------------"
cd $LocalTestDir/SingleCopyDir >> $LOGFILE 2>&1
for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now verifying $FileName checksum after copy" >> $LOGFILE
	echo  "- - -  Now verifying $FileName checksum after copy"
	sha1sum -c $LocalTestDir/CreateFiles/$FileName.sha1 >> $LOGFILE 2>&1
	Sha1ExitCode=$?
	CopyEndTime=`date +%s`
	echo --  Total time to verify sha1sum of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
	if [ ! "$Sha1ExitCode" == "0" ]; then
		echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
		echo "* * *   ERROR  -  Sha1sum file not created correctly for $FileName"
	fi
done




#  Mount the irods home dir under FUSE mount point
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Mount the irods home dir under FUSE mount point" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Mount the irods home dir under FUSE mount point"
echo  "----------------------------------------------------------------------"
$iRODSFUSEbin $FUSEParams $FuseMountPoint >> $LOGFILE 2>&1
FuseExitCode=$?
if [ "$FuseExitCode" = "0" ]; then
	echo "- - -  iRODS FUSE exitcode was [$FuseExitCode]" >> $LOGFILE
	echo "- - -  iRODS FUSE exitcode was [$FuseExitCode]"
else
	echo "- - -  iRODS FUSE exited with an error" >> $LOGFILE
	echo "- - -  iRODS FUSE exited with an error"
	echo "- - -  iRODS FUSE exitcode was [$FuseExitCode]" >> $LOGFILE
	echo "- - -  iRODS FUSE exitcode was [$FuseExitCode]"
	exit 1
fi


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now copy individual files from local to fuse_mount" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now copy individual files from local to fuse_mount"
echo  "----------------------------------------------------------------------"

cd $FuseMountPoint >> $LOGFILE 2>&1
if [ ! -d "$FuseMountPoint/FUSE_Testing/SingleCopyDir" ]; then
	mkdir -p $FuseMountPoint/FUSE_Testing/SingleCopyDir >> $LOGFILE 2>&1
else
	rm -rf $FuseMountPoint/FUSE_Testing/SingleCopyDir/* >> $LOGFILE 2>&1
fi

for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now copying $FileName from local to fuse_mount" >> $LOGFILE
	echo  "- - -  Now copying $FileName from local to fuse_mount"
	cp $LocalTestDir/SingleCopyDir/$FileName $FuseMountPoint/FUSE_Testing/SingleCopyDir/ >> $LOGFILE 2>&1
	CopyEndTime=`date +%s`
	echo --  Total time for copy of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now verify individual files just copied to fuse_mount" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now verify individual files just copied to fuse_mount"
echo  "----------------------------------------------------------------------"

cd $FuseMountPoint/FUSE_Testing/SingleCopyDir >> $LOGFILE 2>&1
for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now verifying $FileName sha1sum" >> $LOGFILE
	echo  "- - -  Now verifying $FileName sha1sum"
	sha1sum -c $LocalTestDir/CreateFiles/$FileName.sha1 >> $LOGFILE 2>&1
	Sha1ExitCode=$?
	CopyEndTime=`date +%s`
	echo --  Total time to verify $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
	if [ ! "$Sha1ExitCode" == "0" ]; then
		echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
		echo "* * *   ERROR  -  Sha1sum file not created correctly for $FileName"
	fi
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now delete individual files from local filesystem" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now delete individual files from local filesystem"
echo  "----------------------------------------------------------------------"

for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now deleting $FileName from local SingleCopyDir directory" >> $LOGFILE
	echo  "- - -  Now deleting $FileName from local SingleCopyDir directory"
	rm -rf $LocalTestDir/SingleCopyDir/$FileName >> $LOGFILE 2>&1
	CopyEndTime=`date +%s`
	echo --  Total time for deleting of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now copying individual files from fuse_mount to local" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now copying individual files from fuse_mount to local"
echo  "----------------------------------------------------------------------"

cd $LocalTestDir >> $LOGFILE 2>&1
if [ ! -d "$LocalTestDir/test_downloads/SingleCopyDir" ]; then
	mkdir -p $LocalTestDir/SingleCopyDir >> $LOGFILE 2>&1
else
	rm -rf $LocalTestDir/SingleCopyDir/* >> $LOGFILE 2>&1
fi

for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now copying $FileName from fuse_mount to local" >> $LOGFILE
	echo  "- - -  Now copying $FileName from fuse_mount to local"
	cp $FuseMountPoint/FUSE_Testing/SingleCopyDir/$FileName $LocalTestDir/SingleCopyDir/ >> $LOGFILE 2>&1
	CopyEndTime=`date +%s`
	echo --  Total time for copy of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now verify individual file sha1sum to original file" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now verify individual file sha1sum to original file"
echo  "----------------------------------------------------------------------"
cd $LocalTestDir/SingleCopyDir >> $LOGFILE 2>&1
for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now verifying $FileName checksum after copy" >> $LOGFILE
	echo  "- - -  Now verifying $FileName checksum after copy"
	sha1sum -c $LocalTestDir/CreateFiles/$FileName.sha1 >> $LOGFILE 2>&1
	Sha1ExitCode=$?
	CopyEndTime=`date +%s`
	echo --  Total time to verify sha1sum of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
	if [ ! "$Sha1ExitCode" == "0" ]; then
		echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
		echo "* * *   ERROR  -  Sha1sum file not created correctly for $FileName"
	fi
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now delete individual files from FUSE filesystem" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now delete individual files from FUSE filesystem"
echo  "----------------------------------------------------------------------"

for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now deleting $FileName from FUSE SingleCopyDir directory" >> $LOGFILE
	echo  "- - -  Now deleting $FileName from FUSE SingleCopyDir directory"
	rm -rf $FuseMountPoint/FUSE_Testing/SingleCopyDir/$FileName >> $LOGFILE 2>&1
	CopyEndTime=`date +%s`
	echo --  Total time for deleting of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
done


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Creating files in $FuseMountPoint/FUSE_Testing/CreateFiles" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Creating files in $FuseMountPoint/FUSE_Testing/CreateFiles"
echo  "----------------------------------------------------------------------"
cd $FuseMountPoint/FUSE_Testing >> $LOGFILE 2>&1
if [ ! -d "$FuseMountPoint/FUSE_Testing/CreateFiles" ]; then
	mkdir -p $FuseMountPoint/FUSE_Testing/CreateFiles >> $LOGFILE 2>&1
else
	rm -rf $FuseMountPoint/FUSE_Testing/CreateFiles/* >> $LOGFILE 2>&1
fi
cd $FuseMountPoint/FUSE_Testing/CreateFiles >> $LOGFILE 2>&1


echo  "- - -  Creating 4.5MB file" >> $LOGFILE
echo  "- - -  Creating 4.5MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=4.5MBFile.junk bs=1024 iflag=fullblock count=4608 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 4.5MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 4.5MBFile.junk"
fi


echo  "- - -  Creating 45MB file" >> $LOGFILE
echo  "- - -  Creating 45MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=45MBFile.junk bs=1024 iflag=fullblock count=46080 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 45MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 45MBFile.junk"
fi


echo  "- - -  Creating 450MB file" >> $LOGFILE
echo  "- - -  Creating 450MB file"
CopyStartTime=`date +%s`
dd if=/dev/zero of=450MBFile.junk bs=1024 iflag=fullblock count=460800 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 450MBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 450MBFile.jun"
fi


echo  "- - -  Creating 1.9GB file"
CopyStartTime=`date +%s` >> $LOGFILE
CopyStartTime=`date +%s`
dd if=/dev/zero of=1.9GBFile.junk bs=1024 iflag=fullblock count=2046000 >> $LOGFILE 2>&1
ddExitCode=$?
CopyEndTime=`date +%s`
echo --  Total time for 1.9GBFile.junk file creation was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
if [ ! "$ddExitCode" == "0" ]; then
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
	echo "* * *   ERROR  -  There was an error creating 1.9GBFile.junk"
fi


echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "- - -  Now verify individual file sha1sum to original file" >> $LOGFILE
echo  "----------------------------------------------------------------------" >> $LOGFILE
echo  "----------------------------------------------------------------------"
echo  "- - -  Now verify individual file sha1sum to original file"
echo  "----------------------------------------------------------------------"
cd $FuseMountPoint/FUSE_Testing/CreateFiles >> $LOGFILE 2>&1
for FileName in $FileNameList
do
	CopyStartTime=`date +%s`
	echo  "- - -  Now verifying $FileName checksum after copy" >> $LOGFILE
	echo  "- - -  Now verifying $FileName checksum after copy"
	sha1sum -c $LocalTestDir/CreateFiles/$FileName.sha1 >> $LOGFILE 2>&1
	Sha1ExitCode=$?
	CopyEndTime=`date +%s`
	echo --  Total time to verify sha1sum of $FileName was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
	if [ ! "$Sha1ExitCode" == "0" ]; then
		echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * "
		echo "* * *   ERROR  -  Sha1sum file not created correctly for $FileName"
	fi
done


##  Unmount FUSE mount to be used for testing if already mounted
#cd  >> $LOGFILE 2>&1 
#if [ ! -d "$FuseMountPoint" ]; then
#	mkdir $FuseMountPoint >> $LOGFILE 2>&1
#else
#	FuseMounted=
#	FuseMounted=$(df -h | grep irodsFs)
#	lsof $HOME/fuse_mount
#	if [ ! "$FuseMounted" == "" ]; then
#		fusermount -u $FuseMountPoint >> $LOGFILE 2>&1
#		FMExitCode=$?
#		if [ "$FMExitCode" = "0" ]; then
#			echo "- - -  fusermount exitcode was [$FMExitCode]" >> $LOGFILE
#			echo "- - -  fusermount exitcode was [$FMExitCode]"
#		else
#			echo "- - -  fusermount exited with an error" >> $LOGFILE
#			echo "- - -  fusermount exited with an error"
#			echo "- - -  fusermount exitcode was [$FMExitCode]" >> $LOGFILE
#			echo "- - -  fusermount exitcode was [$FMExitCode]"
#			if [ "$FMExitCode" = "1" ]; then
#				sleep 15
#				fusermount -u $FuseMountPoint >> $LOGFILE 2>&1
#				FMExitCode=$?
#				if [ "$FMExitCode" = "0" ]; then
#					echo "- - -  fusermount exitcode was [$FMExitCode]" >> $LOGFILE
#					echo "- - -  fusermount exitcode was [$FMExitCode]"
#				else
#					echo "- - -  fusermount exited with an error" >> $LOGFILE
#					echo "- - -  fusermount exited with an error"
#					echo "- - -  fusermount exitcode was [$FMExitCode]" >> $LOGFILE
#					echo "- - -  fusermount exitcode was [$FMExitCode]"
#					exit 1
#				fi
#			fi
#		fi
#	else
#		echo "- - -  Fuse mount point is NOT mounted" >> $LOGFILE
#		echo "- - -  Fuse mount point is NOT mounted"
#		rm -rf $FuseMountPoint/* >> $LOGFILE 2>&1
#	fi
#fi


#  Following iget tests should not be run if the fuse mount is active.  FUSE caches
#  information and doesn't update.
#
#echo  "----------------------------------------------------------------------" >> $LOGFILE
#echo  "- - -  Now iget large directory from fuse_mount to local" >> $LOGFILE
#echo  "----------------------------------------------------------------------" >> $LOGFILE
#echo  "----------------------------------------------------------------------"
#echo  "- - -  Now iget large directory from fuse_mount to local"
#echo  "----------------------------------------------------------------------"
#
#
##time iget -r /iplant/home/ipctest/iDropDownloads $LocalTestDir/ >> $LOGFILE 2>&1
#CopyStartTime=`date +%s`
##echo --  Copy Started at -- [ $CopyStartTime ] >> $LOGFILE
#iget -r /iplant/home/ipctest/iDropDownloads $LocalTestDir/ >> $LOGFILE 2>&1
#CopyEndTime=`date +%s`
##echo --  Copy Ended at -- [ $CopyEndTime ] >> $LOGFILE
#echo --  Total time for iget of large directory was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
#
## Remove files just downloaded
#rm -rf $LocalTestDir/test_downloads/iDropDownloads >> $LOGFILE 2>&1


#echo  "----------------------------------------------------------------------" >> $LOGFILE
#echo  "- - -  Now copy large directory from fuse_mount to local" >> $LOGFILE
#echo  "----------------------------------------------------------------------" >> $LOGFILE
#echo  "----------------------------------------------------------------------"
#echo  "- - -  Now copy large directory from fuse_mount to local"
#echo  "----------------------------------------------------------------------"
#
#
##time cp -r $FuseMountPoint/iDropDownloa* $LocalTestDir/
#CopyStartTime=`date +%s`
##echo --  Copy Started at -- [ $CopyStartTime ] >> $LOGFILE
#cp -r $FuseMountPoint/iDropDownloa* $LocalTestDir/ >> $LOGFILE 2>&1
#CopyEndTime=`date +%s`
##echo --  Copy Ended at -- [ $CopyEndTime ] >> $LOGFILE
#echo --  Total time for copy of large directory was [$(( $CopyEndTime - $CopyStartTime ))] seconds >> $LOGFILE
#
#
## Remove files just downloaded
#rm -rf $LocalTestDir/test_downloads/iDropDownloads >> $LOGFILE 2>&1


cd $HOME


# set DONETIME to the current time
DONETIMESECS=`date +%s`
DONETIME=`date +%c`


# echo STARTTIME and DONETIME to the LOGFILE
echo ----------------------------------------------------------------------- >> $LOGFILE
echo --  Start time was - [ $STARTTIME ] >> $LOGFILE
echo --  Done time was -- [ $DONETIME ] >> $LOGFILE
echo --  Total script runtime was [$(( ($DONETIMESECS - $STARTTIMESECS) ))] seconds >> $LOGFILE
echo --  Total script runtime was [$(( ($DONETIMESECS - $STARTTIMESECS) / 60 ))] minutes >> $LOGFILE
echo ----------------------------------------------------------------------- >> $LOGFILE
echo -----------------------------------------------------------------------
echo --  Start time was - [ $STARTTIME ]
echo --  Done time was -- [ $DONETIME ]
echo --  Total script runtime was [$(( ($DONETIMESECS - $STARTTIMESECS) ))] seconds
echo --  Total script runtime was [$(( ($DONETIMESECS - $STARTTIMESECS) / 60 ))] minutes
echo -----------------------------------------------------------------------


#  All Done Now
echo ----------------------------------------------------------------------- >> $LOGFILE
echo --  All Done Now!!!  >> $LOGFILE
echo ----------------------------------------------------------------------- >> $LOGFILE
echo -----------------------------------------------------------------------
echo --  All Done Now!!!
echo -----------------------------------------------------------------------
