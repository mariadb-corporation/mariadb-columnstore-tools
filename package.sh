#!/bin/bash

. VERSION

subDirs=$(find . -maxdepth 1 -mindepth 1 -type d ! -iname ".*" ! -iname "build" ! -iname "resources" ! -iname "cmake" )

tar -zcvf mariadb-columnstore-tools-$COLUMNSTORE_VERSION_MAJOR.$COLUMNSTORE_VERSION_MINOR.$COLUMNSTORE_VERSION_PATCH-$COLUMNSTORE_VERSION_RELEASE.bin.tar.gz COPYRIGHT.txt LICENSE.txt README.md VERSION $subDirs

