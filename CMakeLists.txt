cmake_minimum_required(VERSION 3.16)
project(ToyLisp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)
add_library(toylisp_core src/evaluators/evaluator.cpp)
target_include_directories(toylisp_core PUBLIC src)
target_link_libraries(toylisp_core PUBLIC Qt5::Core)
add_library(toylisp_ui
        src/ui/editor.cpp
        src/ui/mainwindow.cpp
        src/editor/blocks.cpp
        src/editor/brackets.cpp
        resources/icons.qrc)
target_link_libraries(toylisp_ui PUBLIC toylisp_core Qt5::Widgets)
add_executable(toylisp src/app/main.cpp)
target_link_libraries(toylisp PRIVATE toylisp_ui)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(C)
    add_executable(lisp500 src/lisp500/lisp500a64.c)
    set_target_properties(lisp500 PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
            AUTOMOC OFF AUTORCC OFF ENABLE_EXPORTS ON)
    target_link_libraries(lisp500 PRIVATE m ${CMAKE_DL_LIBS})
endif ()
include(CTest)
if (BUILD_TESTING)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Test)
    add_executable(toylisp_tests tests/tests.cpp)
    target_link_libraries(toylisp_tests PRIVATE toylisp_ui Qt5::Test)
    add_test(NAME toylisp_tests COMMAND toylisp_tests)
    add_executable(toylisp_block_tests tests/blocktests.cpp)
    target_link_libraries(toylisp_block_tests PRIVATE toylisp_ui Qt5::Test)
    add_test(NAME toylisp_block_tests COMMAND toylisp_block_tests)
    add_executable(toylisp_bracket_tests tests/brackettests.cpp)
    target_link_libraries(toylisp_bracket_tests PRIVATE toylisp_ui Qt5::Test)
    add_test(NAME toylisp_bracket_tests COMMAND toylisp_bracket_tests)
    add_executable(toylisp_bracket_benchmarks tests/bracketbenchmarks.cpp)
    target_link_libraries(toylisp_bracket_benchmarks PRIVATE toylisp_ui Qt5::Test)
    set_tests_properties(toylisp_tests toylisp_block_tests toylisp_bracket_tests
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if (TARGET lisp500)
        add_executable(lisp500_tests tests/lisp500tests.c)
        set_target_properties(lisp500_tests PROPERTIES
                C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                AUTOMOC OFF AUTORCC OFF ENABLE_EXPORTS ON)
        target_link_libraries(lisp500_tests PRIVATE m ${CMAKE_DL_LIBS})
        add_library(lisp500_test_module MODULE tests/lisp500module.c)
        set_target_properties(lisp500_test_module PROPERTIES
                C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                AUTOMOC OFF AUTORCC OFF)
        add_dependencies(lisp500_tests lisp500_test_module)
        add_test(NAME lisp500_tests
                COMMAND lisp500_tests $<TARGET_FILE:lisp500_test_module>
                ${CMAKE_CURRENT_SOURCE_DIR}/src/lisp500/init500_64.lisp)
        set_tests_properties(lisp500_tests PROPERTIES TIMEOUT 60)
    endif ()
endif ()
