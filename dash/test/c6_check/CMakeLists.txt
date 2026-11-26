# C6 check (ADR 0013, 0019): reads the ESP-Hosted firmware version of the C6
# co-processor over SDIO and starts a short Wi-Fi scan. Does not flash the C6.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(c6_check)
