project(stasm)
cmake_minimum_required(VERSION 2.8.6)

if(${CMAKE_VERSION} VERSION_GREATER 2.8.11)
  cmake_policy(SET CMP0020 NEW)
endif()

# Global Settings
set(CPACK_PACKAGE_NAME "Stasm")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_VERSION_MAJOR 4)
set(CPACK_PACKAGE_VERSION_MINOR 1)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

# find the dependencies
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules/")

set(QT_DEPENDENCIES Concurrent Core Gui Network Sql Svg Widgets Xml)
foreach(QT_DEPENDENCY ${QT_DEPENDENCIES})
  find_package(Qt5${QT_DEPENDENCY})
endforeach()

set(OPENCV_DEPENDENCIES calib3d core features2d flann gpu highgui imgproc ml nonfree objdetect photo video)
find_package( OpenCV REQUIRED )

# Compiler flags
if(UNIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4307 /wd4308 /nologo")
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/stasm)

# Package
set(CPACK_BINARY_BUNDLE       OFF)
set(CPACK_BINARY_DEB          OFF)
set(CPACK_BINARY_DRAGNDROP    OFF)
set(CPACK_BINARY_NSIS         OFF)
set(CPACK_BINARY_OSXX11       OFF)
set(CPACK_BINARY_PACKAGEMAKER OFF)
set(CPACK_BINARY_RPM          OFF)
set(CPACK_BINARY_STGZ         OFF)
set(CPACK_BINARY_TBZ2         OFF)
set(CPACK_BINARY_TGZ          OFF)
set(CPACK_BINARY_TZ           OFF)
set(CPACK_BINARY_ZIP          OFF)
set(CPACK_SOURCE_TGZ          OFF)
set(CPACK_SOURCE_TZ           OFF)
set(CPACK_SOURCE_ZIP          OFF)

if(CMAKE_HOST_WIN32)
  set(CPACK_BINARY_NSIS ON)
  set(CPACK_BINARY_ZIP ON)

  set(CPACK_NSIS_MODIFY_PATH ON)
elseif(CMAKE_HOST_APPLE)
  set(CPACK_BINARY_TBZ2 ON)
else()
  set(CPACK_BINARY_TBZ2 ON)
endif()

include(CPack)
