CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)

IF(UNIX)
    # Avoid warnings in higher versions
    if("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" GREATER 2.6)
        CMAKE_POLICY(VERSION 2.8)
    endif()

    # explicitly set the policy to OLD
    # (cannot use NEW, not everyone is on cmake-2.8.12 yet)
    IF(POLICY CMP0022)
       CMAKE_POLICY(SET CMP0022 OLD)
    ENDIF()

    # We use the LOCATION target property (CMP0026)
    # and get_target_property() for non-existent targets (CMP0045)
    # and INSTALL_NAME_DIR (CMP0042)
    IF(CMAKE_VERSION VERSION_EQUAL "3.0.0" OR
        CMAKE_VERSION VERSION_GREATER "3.0.0")
        CMAKE_POLICY(SET CMP0026 OLD)
        CMAKE_POLICY(SET CMP0045 OLD)
        CMAKE_POLICY(SET CMP0042 OLD)
    ENDIF()
ENDIF(UNIX)

MESSAGE(STATUS "Running cmake version ${CMAKE_VERSION}")

option(BACKUPRESTORE "Build the backup restore tool" ON)
option(MONITORING "Build the monitoring tool" ON)
option(REMOTE_CPIMPORT "Build the mcsimport tool" ON)
option(TEST_RUNNER "Build the test suite" OFF)
option(DEB "Build a DEB" OFF)
option(RPM "Build an RPM" OFF)

if (TEST_RUNNER)
  enable_testing()
endif (TEST_RUNNER)

# Generate "something" to trigger cmake rerun when VERSION changes
CONFIGURE_FILE(
  ${CMAKE_SOURCE_DIR}/VERSION
  ${CMAKE_BINARY_DIR}/VERSION.dep
)

# Read value for a variable from VERSION.

MACRO(COLUMNSTORE_GET_CONFIG_VALUE keyword var)
  IF(NOT ${var})
    FILE (STRINGS ${CMAKE_SOURCE_DIR}/VERSION str REGEX "^[ ]*${keyword}=")
    IF(str)
      STRING(REPLACE "${keyword}=" "" str ${str})
      STRING(REGEX REPLACE  "[ ].*" ""  str "${str}")
      SET(${var} ${str})
    ENDIF()
  ENDIF()
ENDMACRO()

MACRO(GET_COLUMNSTORE_VERSION)
  COLUMNSTORE_GET_CONFIG_VALUE("COLUMNSTORE_VERSION_MAJOR" MAJOR_VERSION)
  COLUMNSTORE_GET_CONFIG_VALUE("COLUMNSTORE_VERSION_MINOR" MINOR_VERSION)
  COLUMNSTORE_GET_CONFIG_VALUE("COLUMNSTORE_VERSION_PATCH" PATCH_VERSION)
  COLUMNSTORE_GET_CONFIG_VALUE("COLUMNSTORE_VERSION_EXTRA" EXTRA_VERSION)
  COLUMNSTORE_GET_CONFIG_VALUE("COLUMNSTORE_VERSION_RELEASE" RELEASE_VERSION)
  

  IF(NOT "${MAJOR_VERSION}" MATCHES "[0-9]+" OR
    NOT "${MINOR_VERSION}" MATCHES "[0-9]+" OR
    NOT "${PATCH_VERSION}" MATCHES "[0-9]+")
    MESSAGE(FATAL_ERROR "VERSION file cannot be parsed.")
  ENDIF()

  SET(VERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}${EXTRA_VERSION}")
  MESSAGE(STATUS "MariaDB-Columnstore ${VERSION}")
  SET(CPACK_PACKAGE_VERSION_MAJOR ${MAJOR_VERSION})
  SET(CPACK_PACKAGE_VERSION_MINOR ${MINOR_VERSION})
  SET(CPACK_PACKAGE_VERSION_PATCH ${PATCH_VERSION}${EXTRA_VERSION})
  SET(PACKAGE_VERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}${EXTRA_VERSION}")
  SET(PACKAGE_RELEASE "${RELEASE_VERSION}")
ENDMACRO()

exec_program("git"
    ${CMAKE_CURRENT_SOURCE_DIR}
    ARGS "describe --match=NeVeRmAtCh --always --dirty"
    OUTPUT_VARIABLE GIT_VERSION)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/gitversionTools.in ${CMAKE_CURRENT_BINARY_DIR}/gitversionTools IMMEDIATE)

# Get columnstore version
GET_COLUMNSTORE_VERSION()

IF(UNIX)
    # use, i.e. don't skip the full RPATH for the build tree
    SET(CMAKE_SKIP_BUILD_RPATH  FALSE)

    # when building, don't use the install RPATH already
    # (but later on when installing)
    SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

    SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

    # add the automatically determined parts of the RPATH
    # which point to directories outside the build tree to the install RPATH
    SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    # the RPATH to be used when installing, but only if it's not a system directory
    LIST(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
    IF("${isSystemDir}" STREQUAL "-1")
        SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    ENDIF("${isSystemDir}" STREQUAL "-1")

    SET(CMAKE_INSTALL_PREFIX "/usr/local/mariadb/columnstore")
    SET(CPACK_RPM_PACKAGE_DEBUG 1)
    SET(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
ENDIF(UNIX)

SET(CPACK_PACKAGE_NAME "mariadb-columnstore-tools")
SET(ENGINE_ARCH "amd64")

SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PACKAGE_VERSION}-${PACKAGE_RELEASE}")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MariaDB ColumnStore: A Scale out Columnar storage engine for MariaDB")
SET(CPACK_PACKAGE_URL "http://www.mariadb.com")
SET(CPACK_PACKAGE_CONTACT "MariaDB Corporation Ab")
SET(CPACK_PACKAGE_SUMMARY "MariaDB ColumnStore: A Scale out Columnar storage engine for MariaDB")
SET(CPACK_PACKAGE_VENDOR "MariaDB Corporation Ab")
SET(CPACK_PACKAGE_LICENSE "Copyright (c) 2018 MariaDB Corporation Ab., all rights reserved; redistributable under the terms of the BSL, see the file LICENSE.txt for details.")

IF(UNIX)
    SET(CPACK_PACKAGE_RELOCATABLE FALSE)
ENDIF(UNIX)

SET (TOOLS_DIR          "${CMAKE_INSTALL_PREFIX}/tools")
SET (BACKUP_RESTORE_DIR "${TOOLS_DIR}/backuprestore")
SET (MONITOR_DIR        "${TOOLS_DIR}/monitoring")
SET (MCSIMPORT_DIR      "${TOOLS_DIR}/mcsimport")

IF(BACKUPRESTORE)
  ADD_SUBDIRECTORY(backuprestore)
ENDIF(BACKUPRESTORE)

IF(MONITORING)
  ADD_SUBDIRECTORY(monitoring)
ENDIF(MONITORING)

IF(UNIX)
  INSTALL(FILES COPYRIGHT.txt LICENSE.txt README.md VERSION DESTINATION ${TOOLS_DIR} )
  INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/gitversionTools DESTINATION ${TOOLS_DIR} )
ENDIF(UNIX)
#INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/gitversionTools DESTINATION ${TOOLS_DIR} )

IF(REMOTE_CPIMPORT)
  add_subdirectory(mcsimport)
ENDIF(REMOTE_CPIMPORT)

IF(DEB)

  CMAKE_MINIMUM_REQUIRED(VERSION 3.4)

  SET(CPACK_GENERATOR "DEB")

  IF (NOT CPACK_DEBIAN_PACKAGE_VERSION)
      SET (CPACK_DEBIAN_PACKAGE_VERSION ${PACKAGE_VERSION})
  ENDIF()
  IF (NOT CPACK_DEBIAN_PACKAGE_RELEASE)
      SET (CPACK_DEBIAN_PACKAGE_RELEASE ${PACKAGE_RELEASE})
  ENDIF()

  SET(CPACK_DEBIAN_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
  SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-${ENGINE_ARCH}-${DEB}")

  SET(CPACK_DEBIAN_PACKAGE_LICENSE "BSL")
  SET(CPACK_DEBIAN_PACKAGE_RELOCATABLE FALSE)
  SET(CPACK_DEBIAN_PACKAGE_URL ${CPACK_PACKAGE_URL})
  SET(CPACK_DEBIAN_PACKAGE_SUMMARY ${CPACK_PACKAGE_SUMMARY})
  SET(CPACK_DEBIAN_PACKAGE_VENDOR ${CPACK_PACKAGE_VENDOR})
  SET(CPACK_DEBIAN_PACKAGE_LICENSE ${CPACK_PACKAGE_LICENSE})

  SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

  SET(CPACK_DEBIAN_PACKAGE_PROVIDES "mariadb-columnstore-tools")
  IF(REMOTE_CPIMPORT)
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libxml2 , rsync , mariadb-columnstore-api-cpp ( >= ${MAJOR_VERSION}.${MINOR_VERSION} )")
  ELSE(REMOTE_CPIMPORT)
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libxml2 , rsync")
  ENDIF(REMOTE_CPIMPORT)
  SET(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  INCLUDE (CPack)

ENDIF()

IF(RPM)

    SET(CPACK_GENERATOR "RPM")

    IF (NOT CPACK_RPM_PACKAGE_VERSION)
        SET (CPACK_RPM_PACKAGE_VERSION ${PACKAGE_VERSION})
    ENDIF()
    IF (NOT CPACK_RPM_PACKAGE_RELEASE)
        SET (CPACK_RPM_PACKAGE_RELEASE ${PACKAGE_RELEASE})
    ENDIF()

    SET(CPACK_RPM_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
    SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-${ENGINE_ARCH}-${RPM}")

    SET(CPACK_RPM_PACKAGE_LICENSE "BSL")
    SET(CPACK_RPM_PACKAGE_RELOCATABLE FALSE)
    SET(CPACK_RPM_PACKAGE_GROUP "Applications/Databases")
    SET(CPACK_RPM_PACKAGE_URL ${CPACK_PACKAGE_URL})
    SET(CPACK_RPM_PACKAGE_SUMMARY ${CPACK_PACKAGE_SUMMARY})
    SET(CPACK_RPM_PACKAGE_VENDOR ${CPACK_PACKAGE_VENDOR})
    SET(CPACK_RPM_PACKAGE_LICENSE ${CPACK_PACKAGE_LICENSE})

    SET(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})

    # "set/append array" - append a set of strings, separated by a space
    MACRO(SETA var)
        FOREACH(v ${ARGN})
            SET(${var} "${${var}} ${v}")
        ENDFOREACH()
    ENDMACRO(SETA)

    SETA(CPACK_RPM_PACKAGE_PROVIDES "mariadb-columnstore-tools")
    IF(REMOTE_CPIMPORT)
        SETA(CPACK_RPM_PACKAGE_REQUIRES "libxml2" "rsync" "mariadb-columnstore-api-cpp >= ${MAJOR_VERSION}.${MINOR_VERSION}")
    ELSE(REMOTE_CPIMPORT)
        SETA(CPACK_RPM_PACKAGE_REQUIRES "libxml2" "rsync")
    ENDIF(REMOTE_CPIMPORT)


    SET(CPACK_RPM_SPEC_MORE_DEFINE "${CPACK_RPM_SPEC_MORE_DEFINE}
        %define ignore \#
    ")

    SET(ignored
        "%ignore /usr"
        "%ignore /usr/local"
    )

    SET(CPACK_RPM_USER_FILELIST ${ignored})

    INCLUDE (CPack)

ENDIF()

if(WIN32)
      IF(REMOTE_CPIMPORT)
        # On Windows find the Microsoft Visual Studio 2017 C++ Redistributable merge module (x64) that is packaged with mcsapi
          
        # Macro to get the subdirectories of a given directory
        MACRO(SUBDIRLIST result curdir)
            FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
            SET(dirlist "")
            FOREACH(child ${children})
              IF(IS_DIRECTORY ${curdir}/${child})
                LIST(APPEND dirlist ${child})
              ENDIF()
            ENDFOREACH()
           SET(${result} ${dirlist})
        ENDMACRO()
  
        # Build up potential merge module paths
        SET(VISUAL_STUDIO_PATHS)
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles}/Microsoft Visual Studio/2017/Community/VC/Redist/MSVC")
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles} (x86)/Microsoft Visual Studio/2017/Community/VC/Redist/MSVC")
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles}/Microsoft Visual Studio/2017/Enterprise/VC/Redist/MSVC")
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles} (x86)/Microsoft Visual Studio/2017/Enterprise/VC/Redist/MSVC")
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles}/Microsoft Visual Studio/2017/Professional/VC/Redist/MSVC")
        LIST(APPEND VISUAL_STUDIO_PATHS "$ENV{ProgramFiles} (x86)/Microsoft Visual Studio/2017/Professional/VC/Redist/MSVC")
        
        SET(MSM_PATHS)
        FOREACH(VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATHS})
          file(TO_CMAKE_PATH ${VISUAL_STUDIO_PATH} VISUAL_STUDIO_PATH_CONV)
          SUBDIRLIST(SUB_DIRS ${VISUAL_STUDIO_PATH_CONV})
          FOREACH(SUB_DIR ${SUB_DIRS})
              LIST(APPEND MSM_PATHS "${VISUAL_STUDIO_PATH}/${SUB_DIR}/MergeModules")
          ENDFOREACH()
        ENDFOREACH()
      
        # Try to find the merge module
        find_file(
          VS_2017_REDIST_MSM
          NAMES Microsoft_VC141_CRT_x64.msm Microsoft_VC140_CRT_x64.msm
          HINTS $ENV{MSM_DIRECTORY}
          PATHS ${MSM_PATHS}
        )
        if (${VS_2017_REDIST_MSM} STREQUAL "VS_2017_REDIST_MSM-NOTFOUND")
          MESSAGE(FATAL_ERROR "Could not find a usable Visual Studio 2015/17 C++ Redistributable merge module Microsoft_VC141_CRT_x64.msm!\nYou can hint cmake by setting the environment variable MSM_DIRECTORY.")
        ENDIF()
        MESSAGE(STATUS "Found Visual Studio 2015/17 C++ Redistributable merge module: ${VS_2017_REDIST_MSM}")
        
        SET(CPACK_GENERATOR "WIX")
        SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
        SET(CPACK_WIX_UI_BANNER "${CMAKE_CURRENT_SOURCE_DIR}/resources/WixUIBannerBmp.jpg")
        SET(CPACK_WIX_UI_DIALOG "${CMAKE_CURRENT_SOURCE_DIR}/resources/WixUIDialogBmp.jpg")
        SET(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/mariadb.ico")
        SET(WIX_PATCH_FILES)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_windows/redist.patch.in"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_windows/redist.patch"
            IMMEDIATE @ONLY)
        LIST(APPEND WIX_PATCH_FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_windows/redist.patch")
        SET(CPACK_WIX_PATCH_FILE ${WIX_PATCH_FILES})
        SET(CPACK_PACKAGE_NAME "MariaDB ColumnStore mcsimport")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MariaDB ColumnStore mcsimport")
        set(CPACK_PACKAGE_SUMMARY "MariaDB ColumnStore: A Scale out Columnar storage engine for MariaDB")
        set(CPACK_PACKAGE_VENDOR "MariaDB Corporation Ab")
        SET(CPACK_PACKAGE_CONTACT "MariaDB Corporation Ab")
        SET(ENGINE_ARCH "x64")
        SET(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
        set(CPACK_PACKAGE_VERSION_RELEASE ${PACKAGE_RELEASE})
        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_VERSION_RELEASE}-${ENGINE_ARCH}")
        set(CPACK_WIX_UPGRADE_GUID "62d22168-cbc0-47f7-8875-e4e3ac80552c")
        set(CPACK_WIX_CMAKE_PACKAGE_REGISTRY ON)
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "MariaDB/ColumnStore Remote Import")
        include(CPack)
    ENDIF(REMOTE_CPIMPORT)
endif(WIN32)

if (TEST_RUNNER)
  include(CTest)
endif (TEST_RUNNER)

MESSAGE(STATUS "-----------------------------------------------")
MESSAGE(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
MESSAGE(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "TEST_RUNNER = ${TEST_RUNNER}")
MESSAGE(STATUS "BACKUPRESTORE = ${BACKUPRESTORE}")
MESSAGE(STATUS "MONITORING = ${MONITORING}")
MESSAGE(STATUS "REMOTE_CPIMPORT = ${REMOTE_CPIMPORT}")
MESSAGE(STATUS "RPM = ${RPM}")
MESSAGE(STATUS "DEB = ${DEB}")
MESSAGE(STATUS "Change a values with: cmake -D<Variable>=<Value>")
MESSAGE(STATUS "------------------------------------------------")
MESSAGE(STATUS)

