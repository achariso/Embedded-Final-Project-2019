cmake_minimum_required(VERSION 3.13)
project(Final C)

set(CMAKE_C_STANDARD 99)

set(FINAL_SOURCES client.c server.c utils.c log.c communication.c)
add_library(FINAL_LIB ${FINAL_SOURCES})

target_link_libraries(Final FINAL_LIB pthread)