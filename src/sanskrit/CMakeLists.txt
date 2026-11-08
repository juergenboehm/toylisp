cmake_minimum_required(VERSION 3.16)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(Sanskrit LANGUAGES CXX)
    include(CTest)
endif()

find_package(utf8proc CONFIG QUIET)
if(TARGET utf8proc::utf8proc)
    set(sanskrit_unicode utf8proc::utf8proc)
elseif(TARGET utf8proc)
    set(sanskrit_unicode utf8proc)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SANSKRIT_UTF8PROC REQUIRED IMPORTED_TARGET libutf8proc)
    set(sanskrit_unicode PkgConfig::SANSKRIT_UTF8PROC)
endif()

add_library(sanskrit sanskrit.cpp transliteration.cpp)
target_include_directories(sanskrit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(sanskrit PUBLIC cxx_std_17)
target_link_libraries(sanskrit PRIVATE ${sanskrit_unicode})
set_target_properties(sanskrit PROPERTIES AUTOMOC OFF AUTORCC OFF CXX_EXTENSIONS OFF)

option(SANSKRIT_BUILD_QT_SERVICE "Build the Qt network and Python backend" OFF)
if(SANSKRIT_BUILD_QT_SERVICE)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Core Network)
    add_library(sanskrit_qt
            sanskritserviceqt.cpp
            sanskritparser.cpp
            ../lookups/httpclient.cpp
            ../processes/jsonprocess.cpp
            sanskrit_python.qrc)
    target_link_libraries(sanskrit_qt PUBLIC sanskrit Qt5::Core Qt5::Network)
    set_target_properties(sanskrit_qt PROPERTIES AUTOMOC ON AUTORCC ON CXX_EXTENSIONS OFF)
endif()

if(BUILD_TESTING)
    add_executable(sanskrit_tests ../../tests/sanskrittests.cpp)
    target_link_libraries(sanskrit_tests PRIVATE sanskrit)
    set_target_properties(sanskrit_tests PROPERTIES AUTOMOC OFF AUTORCC OFF CXX_EXTENSIONS OFF)
    add_test(NAME sanskrit_tests COMMAND sanskrit_tests)
endif()
