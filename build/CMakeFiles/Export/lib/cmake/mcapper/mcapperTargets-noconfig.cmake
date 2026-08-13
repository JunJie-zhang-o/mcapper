#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "mcapper::mcapper" for configuration ""
set_property(TARGET mcapper::mcapper APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(mcapper::mcapper PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libmcapper.so.0.1.0"
  IMPORTED_SONAME_NOCONFIG "libmcapper.so.0"
  )

list(APPEND _IMPORT_CHECK_TARGETS mcapper::mcapper )
list(APPEND _IMPORT_CHECK_FILES_FOR_mcapper::mcapper "${_IMPORT_PREFIX}/lib/libmcapper.so.0.1.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
