cmake_minimum_required(VERSION 3.23)
project(byu_cs324 C)

set(CMAKE_C_STANDARD 11)

include_directories(lab-shell-signals)

add_executable(byu_cs324
        lab-shell-signals/tsh.c)
