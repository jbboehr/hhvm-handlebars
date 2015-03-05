FIND_PATH(HANDLEBARS_INCLUDE_DIR NAMES handlebars.h PATHS /usr/include /usr/local/include)
FIND_LIBRARY(HANDLEBARS_LIBRARY NAMES handlebars PATHS /lib /usr/lib /usr/local/lib)

IF (HANDLEBARS_INCLUDE_DIR AND HANDLEBARS_LIBRARY)
    MESSAGE(STATUS "handlebars Include dir: ${HANDLEBARS_INCLUDE_DIR}")
    MESSAGE(STATUS "handlebars library: ${HANDLEBARS_LIBRARY}")
ELSE()
    MESSAGE(FATAL_ERROR "Cannot find handlebars library")
ENDIF()

INCLUDE_DIRECTORIES(${HANDLEBARS_INCLUDE_DIR})

SET(CMAKE_CXX_FLAGS_DEBUG "-g -pg -O0")

HHVM_EXTENSION(handlebars handlebars.cpp)
HHVM_SYSTEMLIB(handlebars ext_handlebars.php)

TARGET_LINK_LIBRARIES(handlebars ${HANDLEBARS_LIBRARY})
