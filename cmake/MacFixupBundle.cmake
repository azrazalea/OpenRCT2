include(BundleUtilities)

if(NOT DEFINED INPUT_APP OR INPUT_APP STREQUAL "")
    message(FATAL_ERROR "INPUT_APP must be defined when running MacFixupBundle.cmake")
endif()

set(BU_CHMOD_BUNDLE_ITEMS ON)

set(FIXUP_SEARCH_DIRS)
if(DEFINED SEARCH_PATH AND NOT SEARCH_PATH STREQUAL "")
    list(APPEND FIXUP_SEARCH_DIRS "${SEARCH_PATH}")
endif()

message(STATUS "Running fixup_bundle on ${INPUT_APP}")
fixup_bundle("${INPUT_APP}" "" "${FIXUP_SEARCH_DIRS}")
