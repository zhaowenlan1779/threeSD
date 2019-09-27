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
  ncch/ncch_container.cpp
  ncch/ncch_container.h
  ncch/smdh.cpp
  ncch/smdh.h
  ncch/title_metadata.cpp
  ncch/title_metadata.h
  quick_decryptor.cpp
  quick_decryptor.h
  result_status.h
)

target_link_libraries(core PRIVATE common cryptopp)
