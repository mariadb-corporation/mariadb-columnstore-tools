#!/bin/bash

. VERSION

subDirs=$(find . -maxdepth 1 -mindepth 1 -type d ! -iname ".*" )

tar -zcvf mariadb-columnstore-tools-$COLUMNSTORE_VERSION_MAJOR.$COLUMNSTORE_VERSION_MINOR.$COLUMNSTORE_VERSION_PATCH-$COLUMNSTORE_VERSION_RELEASE.tar.gz COPYRIGHT.txt LICENSE.txt README.md VERSION $subDirs

