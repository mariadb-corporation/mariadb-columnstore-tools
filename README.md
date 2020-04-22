# MariaDB ColumnStore Backup
This provides scripts to backup and restore the data on a columnstore cluster.

For more details on each tool see the README.md in the backuprestore directory. For licensing details, please see LICENSE.txt.

## Packaging

### CMake Options

A couple packaging options are available when executing CMake by using the following
command line:

```shell
cmake -D<Variable>=<Value>
```

The options are as follows:

| Option | Default | Definition |
| ------ | ------ | ---------- |
| ``RPM`` | ``OFF`` | Build a RPM (and the OS name for the package) |
| ``DEB`` | ``OFF`` | Build a DEB (and the OS name for the package) |

### CentOS 7
```bash
mkdir build && cd build
cmake .. -DRPM=centos7
make package
```

### Debian 8 & 9
```bash
mkdir build && cd build
cmake .. -DDEB={jessie,stretch}
make package
```

### Ubuntu 16 & 18 LTS
```bash
mkdir build && cd build
cmake .. -DDEB={xenial,bionic}
make package
```

