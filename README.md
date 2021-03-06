# MariaDB ColumnStore Tools
This provides a set of useful tools for MariaDB ColumnStore and currently includes:
1. monitoring - An Example monitoring script
2. backuprestore - Backup and Restore tools providing automation for the ColumnStore backup procedure
3. remote cpimport / mcsimport - A tool to remotely inject data into ColumnStore utilizing MariaDB's Bulk Write SDK

For more details on each tool see the README.md in each directory. For licensing details, please see LICENSE.txt.

## Packaging

### CMake Options

Several options are available when execution CMake by using the following
command line:

```shell
cmake -D<Variable>=<Value>
```

Alternatively you can use one of the CMake GUIs to set the options.

The options are as follows:

| Option | Default | Definition |
| ------ | ------ | ---------- |
| ``TEST_RUNNER`` | ``OFF`` | Build the test suite |
| ``RPM`` | ``OFF`` | Build a RPM (and the OS name for the package) |
| ``DEB`` | ``OFF`` | Build a DEB (and the OS name for the package) |
| ``BACKUPRESTORE`` | ``ON`` | Build the backup restore tool |
| ``MONITORING`` | ``ON`` | Build the monitoring tool |
| ``REMOTE_CPIMPORT`` | ``ON`` | Build the remote cpimport / mcsimport tool |

### CentOS 6
**Note:** remote cpimport can't be built on CentOS 6 as it requires mcsapi that isn't available on CentOS 6.

```bash
mkdir build && cd build
cmake .. -DREMOTE_CPIMPORT=OFF
make
cmake .. -DRPM=centos6
sudo make package
```

### CentOS 7
```bash
mkdir build && cd build
cmake .. -DTEST_RUNNER=ON
make -j2
sudo make install
ctest -V
cmake .. -DRPM=centos7
sudo make package
```

### DEB
```bash
mkdir build && cd build
cmake .. -DTEST_RUNNER=ON
make -j2
sudo make install
ctest -V
cmake .. -DDEB=stretch
sudo make package
```

### MSI
Currently only mcsimport can be built and used on Windows.

To compile it you first have to install the Windows version of mcsapi and set the environment variable `MCSAPI_INSTALL_DIR` to its top level installation directory.

Afterwards you can generate the package through following commands in Visual Studio 2017's "x64 Native Tools Command Prompt for VS 2017":
```bash
mkdir build && cd build
cmake -DTEST_RUNNER=ON -G "Visual Studio 14 2015 Win64" ..
cmake --build . --config RelWithDebInfo --target package
ctest -C RelWithDebInfo -V
signtool.exe sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a "MariaDB ColumnStore mcsimport-*-x64.msi"
```
**NOTE**  
For testing you have to set the environment variables `MCSAPI_CS_TEST_IP`, `MCSAPI_CS_TEST_PASSWORD`, `MCSAPI_CS_TEST_USER`, and `COLUMNSTORE_INSTALL_DIR`. Please see the additional instructions in mcsimport's sub-directory.
