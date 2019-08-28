add_library(core STATIC
  data_container.cpp
  data_container.h
  decryptor.cpp
  decryptor.h
  importer.cpp
  importer.h
  inner_fat.cpp
  inner_fat.h
  key/arithmetic128.cpp
  key/arithmetic128.h
  key/key.cpp
  key/key.h
)

target_link_libraries(core PRIVATE common cryptopp)
