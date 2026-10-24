cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab14_ztwister_tests)

# The test file + the module under test (from ../src).
target_sources(app PRIVATE src/test_temp.c ../src/temp.c)
target_include_directories(app PRIVATE ../src)
