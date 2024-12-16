cmake_minimum_required(VERSION 3.29)
project(lab C)

set(CMAKE_C_STANDARD 11)


##ускорение
#if(NOT CMAKE_BUILD_TYPE)
#    set(CMAKE_BUILD_TYPE Release)
#endif()
#set(CMAKE_C_FLAGS "-Wall -Wextra")
#set(CMAKE_C_FLAGS_DEBUG "-g")
##агрессивная
#set(CMAKE_C_FLAGS_RELEASE "-O3")

add_executable(lab main.c
)
