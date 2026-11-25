# Flash endurance test for the core board (ESP32-S3).
# Uses the partition table of core (../../partitions.csv).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(flash_endurance)
