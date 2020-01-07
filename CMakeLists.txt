cmake_minimum_required(VERSION 3.00)

project(Pyramid VERSION 0.1)
#Pretty sure that dbus-cxx-1.0 requires C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(pyramid main.cpp src/PyramidASRService.cpp src/PyramidASRServiceAdapter.cpp src/SphinxDecoder.cpp)

target_include_directories(pyramid PUBLIC "${PROJECT_BINARY_DIR}" "${PROJECT_BINARY_DIR}/include")

#We're gonna use pkg-config to include all of our libraries
include(FindPkgConfig)
pkg_check_modules(SPHINXBASE REQUIRED sphinxbase)
pkg_check_modules(POCKETSPHINX REQUIRED pocketsphinx)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(DBUSCXX REQUIRED dbus-cxx-1.0)
pkg_check_modules(BASR REQUIRED buckey-asr)

#Set ENABLE_DEBUG to ON to enable debug log messages
option(ENABLE_DEBUG "Enable debugging messages" OFF)

#Set to ON to use start_stream pocketsphinx function, this *might* improve accuracy, or make it worse
option(ENABLE_STREAM_DECODING "Enable the usage of ps_start_stream during decoding" ON)

set(DEFAULT_LM_PATH "/usr/local/share/pocketsphinx/model/en-us/en-us.lm.bin")
set(DEFAULT_HMM_PATH "/usr/local/share/pocketsphinx/model/en-us/en-us/")
set(DEFAULT_DICT_PATH "/usr/local/share/pocketsphinx/model/en-us/cmudict-en-us.dict")
set(DEFAULT_LOG_PATH "decoder.log")

configure_file(config.h.in config.h)
configure_file(res/pyramid.conf.in res/pyramid.conf)

if (ENABLE_DEBUG)
    #add_compile_definitions(ENABLE_DEBUG)
    add_definitions(-DENABLE_DEBUG)
endif()

if (ENABLE_STREAM_DECODING)
    add_definitions(-DENABLE_PS_STREAM)
endif()

target_include_directories(pyramid PUBLIC "${GLIB_INCLUDE_DIRS}" "${DBUSCXX_INCLUDE_DIRS}" "${BASR_INCLUDE_DIRS}" "${SPHINXBASE_INCLUDE_DIRS}" "${POCKETSPHINX_INCLUDE_DIRS}")

target_link_libraries(pyramid PUBLIC "${GLIB_LDFLAGS}" "${DBUSCXX_LDFLAGS}" "${BASR_LDFLAGS}" "${SPHINXBASE_LDFLAGS}" "${POCKETSPHINX_LDFLAGS}")

#Install the binary
install(TARGETS pyramid DESTINATION /usr/bin)

#Install the default config file
install(FILES res/pyramid.conf res/confirm.gram DESTINATION /etc/pyramid)
