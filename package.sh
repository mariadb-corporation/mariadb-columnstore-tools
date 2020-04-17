#!/bin/bash

. VERSION
tarName=MariaDB-columnstore-backup-$COLUMNSTORE_VERSION_MAJOR.$COLUMNSTORE_VERSION_MINOR.$COLUMNSTORE_VERSION_PATCH-$COLUMNSTORE_VERSION_RELEASE.bin.tar
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )" #get the absolute diretory of this script

#subDirs=$(find . -maxdepth 1 -mindepth 1 -type d ! -iname ".*" ! -iname "mcsimport" ! -iname "build" ! -iname "resources" ! -iname "cmake" )

tar -cvf $tarName COPYRIGHT.txt LICENSE.txt README.md VERSION backuprestore/columnstore{Backup,Restore} --exclude=CMakeLists.txt

# Compress the archive
cd $DIR
gzip $tarName
