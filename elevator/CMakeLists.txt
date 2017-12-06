cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED 11)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(flag_var
            CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "-O[^\\s]?" "" ${flag_var} ${${flag_var}})
        set(${flag_var} "${${flag_var}} -O3 -Wall -Wextra -Werror")
    endforeach(flag_var)
endif()

project(elevator)
add_executable(${PROJECT_NAME} "main.cpp")

if (UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

