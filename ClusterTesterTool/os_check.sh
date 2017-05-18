#!/bin/sh
# Detects which OS and if it is Linux then it will detect which Linux
# Distribution.

OS=`uname -s`
LSB=`which lsb_release 2>/dev/null`

GetVersionFromFile()
{
    VERSION=`cat $1 | tr "\n" ' ' | sed s/.*VERSION.*=\ // `
}

if [ "${OS}" = "SunOS" ] ; then
    NAME="${OS} ${REV}(${ARCH} `uname -v`)"
elif [ "${OS}" = "AIX" ] ; then
    NAME="${OS} `oslevel` (`oslevel -r`)"
elif [ "${OS}" = "Linux" ] ; then
    if [ -f /etc/redhat-release ] ; then
	NAME=`cat /etc/redhat-release`
    elif [ -f /etc/SuSE-release ] ; then
        NAME=`cat /etc/SuSE-release | tr "\n" ' '| sed s/VERSION.*//`
    elif [ -n "$LSB" ]; then
	NAME="`lsb_release -d | cut -f2`"
    elif [ -f /etc/debian_version ] ; then
	NAME="Debian `cat /etc/debian_version`"
    else
	echo "Unknown OS version"
	exit 1
    fi

fi

echo ${NAME}
exit 0 
