#columnstoreBackup / columnstoreRestore README

#columnstoreBackup

##BACKGROUND

High level steps involved in performing a backup of MariaDB ColumnStore are:

1. Suspend write activity on the system. 
2. Backup the MariaDB Server instance. 
3. Backup the ColumnStore data files. 
4. Resume write activity on the system. 

columnstoreBackup will automate these steps to streamline the process.

WARNING: Currently columnstoreBackup is only for ColumnStore data backups. Other engines may not be fully backed up and data could be lost when restoring.

##SETUP

To run columnstoreBackup you'll need to setup a backup server with passwordless ssh login available for the user account that installed MariaDB ColumnStore. (Default: root)

Copy the executable columnstoreBackup onto the backup server. Create a target directory on the backup server to store the files. This directory will need to have enough space to store all ColumnStore data files.

> EXAMPLE
>
> Backup Executable: /home/user/columnstoreBackup
>
> Backup Data Directory: /home/user/columnstoreBackupData/



There is an optional columnstoreBackup.config file that when placed in the same directory as the columnstoreBackup executable will allow you to configure an incremental backup option that uses the rsync link-dest option to enable incremental backups. These are stored in backup.1 thru backup.[n-1] from newest to oldest. The columnstoreBackup.config file should only contain a single line:
> NUMBER_BACKUPS=[n]

Where "n" is the number of incremental backups to store. (Default: 3)


##RUNNING

columnstoreBackup must be run as root user either logging in as root or via the sudo command.
```bash
Usage: [sudo] ./columnstoreBackup [options] activeParentOAM backupServerLocation

activeParentOAM           IP address of ColumnStore server
                             (Active parent OAM module on multi-node install)
backupServerLocation      Path to the directory for storing backup files.

OPTIONS:
-h,--help         Prints help and exits.
-v,--verbose      Print more verbose execution details.
-d,--dry-run      Dry run and executes rsync dry run with stats.
-z,--compress     Utilize the compression option for rsync.
-n [value]        Maximum number parallel rsync commands. (Default: 5)
--user=[user]     Change the user performing remote sessions. (Default: root)

--install-dir=[PATH]  Change the install directory of ColumnStore.
                          Default: /usr/local/mariadb/columnstore
```

> EXAMPLE
> 
> Running from the directory /home/user/:
>
> sudo ./columnstoreBackup -zv 192.168.1.2 home/user/columnstoreBackupData

This will execute a backup for the system with a parent OAM module located at 192.168.1.2 and store all backup files inside the directory located at home/user/columnstoreBackupData. Option v will print out a more verbose logging of commands executed and option z will let rsync utilize the compression option for file transfers.


##LOGGING

Logging is output to the console as well as to a columnstoreBackup.log that is located in the directory columnstoreBackup is executed. This will contain some extra details on some issues. Log rotation is left to the user for handling.

##Return Codes

* 0   - success
* 1   - command line parameter or config file issue detected
* 2   - missing rsync or xmllint
* 3   - detected issue with disk space
* 4   - detected bad configuration file settings
* 5   - rsync command failed with an error
* 255 - could not connect via passwordless ssh

##Operation Notes

columnstoreBackup will create the following directories inside the Backup Data Directory:

* backup.[1-n] (n incremental backups)
* cnf (my.cnf and my.cnf.d)
* pm[moduleID]dbroot[DBRootID] (pm1dbroot1 contains PM data from dbroot 1 on pm 1)
* um[moduleID] (NOTE: When UM/PM are combined on nodes UM1 is the mysql/db directory for PM1)

These directories are created if they do not exist and can be created prior to execution by the user.

The columnstoreBackup option -n [value] limits the number parallel rsync commands executed at a given time. The default 5 means up to 5 DBRoots will kick off rysnc commands to various PMs and the backup system will wait until all are complete and verified successful. At this time it will kick off another 5 DBRoots. The progress indicator should reflect the percentage of total completion and not individual rysnc commands. This value can be set higher via the -n command but if the number of DBRoots present in the system is large enough there may be a performance hit on system processing or network bandwidth limitations. 

#columnstoreRestore

The tool is designed to be run on the system storing the backups. This will automate restoring from backups created by the columnstoreBackup tool.

##SETUP

To run columnstoreRestore you'll need to setup a backup server with passwordless ssh login available for the user account that installed MariaDB ColumnStore. (Default: root)

columnstoreRestore must be run as root or with sudo.

columnstoreRestore expects MariaDB Columnstore to be shutdown in a fresh install state.

Take the following steps to prepare system for columnstoreRestore:

1. On the active parent OAM module execute the command
```bash
mcsadmin shutdownsystem y
```

2. Run on all PM modules:
```bash
rm -rf [INSTALL_DIR]/data*/000.dir
```

3. Run on all UM or combo PM front-end nodes
```bash
rm -rf [INSTALL_DIR]/data1/systemFiles/dbrm/*
```

4. Run on all PMs
```bash
cd [INSTALL_DIR]/mysql/db 
```
5. delete all directories except:
* calpontsys
* infinidb_Qquerystats
* infinidb_infinidb_vtable
* mysql
* performance_schema
* test

6. On the active parent OAM module execute the command
```bash
[INSTALL_DIR]/bin/clearShm
```

7. On the backup system run columnstoreRestore script


##RUNNING

columnstoreRestore must be run as root user either logging in as root or via the sudo command.
```bash
Usage: ./columnstoreRestore [options] backupServerLocation restoreServerPM1

restoreServerPM1          IP address of ColumnStore server
                             (Assumes PM1 = Active Parent OAM Module)
backupServerLocation      Path to the directory for storing backup files.

OPTIONS:
-h,--help         Print this message and exit.
-v,--verbose      Print more verbose execution details.
-d,--dry-run      Dry run and executes rsync dry run with stats.
-z,--compress     Utilize the compression option for rsync.
-n [value]        Maximum number parallel rsync commands. (Default: 5).
--user=[user]     Change the user performing remote sessions. (Default: root)

--install-dir=[PATH]  Change the install directory of ColumnStore.
                          Default: /usr/local/mariadb/columnstore

```
> EXAMPLE
> 
> Running from the directory /home/user/ with the columnstoreBackupData directory created in the columnstoreBackup example above:
>
> sudo ./columnstoreRestore -zv home/user/columnstoreBackupData 192.168.1.100

This will execute a restore for the MariaDB ColumnStore system with a parent OAM module located at 192.168.1.100 from the directory located at home/user/columnstoreBackupData. Option v will print out a more verbose logging of commands executed and option z will let rsync utilize the compression option for file transfers.


##LOGGING

Logging is output to the console as well as to a columnstoreRestore.log that is located in the directory columnstoreBackup is executed. This will contain some extra details on some issues. Log rotation is left to the user for handling.

##Return Codes

* 0   - success
* 1   - command line parameter or config file issue detected
* 2   - missing rsync or xmllint
* 3   - detected issue with disk space
* 4   - detected bad configuration file settings
* 5   - rsync command failed with an error
* 255 - could not connect via passwordless ssh

##Operation Notes

columnstoreRestore will create a restoreConfig directory inside the backupServerLocation defined at command line. This is just meant to store a copy of the restored systems version and configuration file for verification the restore is possible.

The columnstoreRestore option -n [value] limits the number parallel rsync commands executed at a given time. The default 5 means up to 5 DBRoots will kick off rysnc commands to various PMs and the backup system will wait until all are complete and verified successful. At this time it will kick off another 5 DBRoots. The progress indicator should reflect the percentage of total completion and not individual rysnc commands. This value can be set higher via the -n command but if the number of DBRoots present in the system is large enough there may be a performance hit on system processing or network bandwidth limitations. 

