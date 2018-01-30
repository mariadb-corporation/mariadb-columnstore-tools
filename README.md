# MariaDB ColumnStore Tools
This provides a set of useful tools for MariaDB ColumnStore and currently includes:
1. monitoring - An Example monitoring script
2. backuprestore - Backup and Restore tools providing automation for the ColumnStore backup procedure

For more details on each tool see the README.md in each directory. For licensing details, please see LICENSE.txt.

## Packaging

Binary:
```bash
./package.sh
```
RPM:
```bash
cmake . -DRPM=1
make package
```
DEB:
```bash
cmake . -DDEB=1
make package
```