#!/bin/bash

. VERSION
tarName=mariadb-columnstore-tools-$COLUMNSTORE_VERSION_MAJOR.$COLUMNSTORE_VERSION_MINOR.$COLUMNSTORE_VERSION_PATCH-$COLUMNSTORE_VERSION_RELEASE.bin.tar
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )" #get the absolute diretory of this script

subDirs=$(find . -maxdepth 1 -mindepth 1 -type d ! -iname ".*" ! -iname "mcsimport" ! -iname "build" ! -iname "resources" ! -iname "cmake" )

tar -cvf $tarName COPYRIGHT.txt LICENSE.txt README.md VERSION $subDirs --exclude=CMakeLists.txt

# if a build dir was specified use it and include the binaries
if [ $# -ge 1 ]; then
  if [ -d $1 ]; then

    # mcsimport
    if [ -f $DIR/$1/mcsimport/mcsimport ]; then
      tar -rvf $tarName ./mcsimport --exclude=*.txt --exclude=*.cpp --exclude=test
      cd $1
      tar -rvf $DIR/$tarName ./mcsimport/mcsimport
      cd $DIR
    fi
  else
    echo "error: specified cmake build dir $1 could't be found"
    exit 2
  fi
fi

# Compress the archive
cd $DIR
gzip $tarName
