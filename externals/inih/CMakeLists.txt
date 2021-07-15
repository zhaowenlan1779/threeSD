add_library(inih
    inih/ini.c
    inih/ini.h
    inih/cpp/INIReader.cpp
    inih/cpp/INIReader.h
)
target_include_directories(inih INTERFACE .)

target_compile_definitions(inih PRIVATE -DINI_MAX_LINE=1000)
