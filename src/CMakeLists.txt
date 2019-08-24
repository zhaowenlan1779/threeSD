include_directories(.)

add_executable(threeSD)

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(frontend)

target_link_libraries(threeSD PRIVATE cryptopp fmt)
