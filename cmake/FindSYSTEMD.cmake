# Find the systemd includes and library
#
#  SYSTEMD_INCLUDE_DIRS - where to find systemd/sd-daemon.h
#  SYSTEMD_LIBRARIES    - List of libraries when using libsystemd
#  SYSTEMD_FOUND        - True if libsystemd found

find_package(PkgConfig)
pkg_search_module(SYSTEMD libsystemd)

find_path(SYSTEMD_INCLUDE_DIR
  NAMES systemd/sd-daemon.h
  HINTS
    "${SYSTEMD_INCLUDE_DIRS}"
    "${SYSTEMD_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(SYSTEMD_LIBRARY
  NAMES systemd
  HINTS
    "${SYSTEMD_LIBRARY_DIRS}"
    "${SYSTEMD_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SYSTEMD DEFAULT_MSG SYSTEMD_LIBRARY
    SYSTEMD_INCLUDE_DIR)

if(SYSTEMD_FOUND)
  set( SYSTEMD_INCLUDE_DIRS ${SYSTEMD_INCLUDE_DIR} )
  set( SYSTEMD_LIBRARIES ${SYSTEMD_LIBRARY} )
else()
  set( SYSTEMD_INCLUDE_DIRS )
  set( SYSTEMD_LIBRARIES )
endif()

mark_as_advanced( SYSTEMD_LIBRARIES SYSTEMD_INCLUDE_DIRS )
