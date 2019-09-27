add_library(common STATIC
  alignment.h
  assert.h
  bit_field.h
  common_funcs.h
  common_paths.h
  common_types.h
  file_util.cpp
  file_util.h
  logging/log.cpp
  logging/log.h
  misc.cpp
  scope_exit.h
  string_util.cpp
  string_util.h
  swap.h
  thread.h
)

target_link_libraries(common PUBLIC fmt)
