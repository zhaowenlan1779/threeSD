set(CMAKE_AUTOMOC ON)

add_definitions(-DUNICODE -D_UNICODE)

add_library(qdevicewatcher STATIC
  qdevicewatcher/src/qdevicewatcher_p.h
  qdevicewatcher/src/qdevicewatcher.cpp
  qdevicewatcher/src/qdevicewatcher.h
)

target_link_libraries(qdevicewatcher PRIVATE Qt5::Core)
target_include_directories(qdevicewatcher INTERFACE qdevicewatcher/src)

if (WIN32)
    target_sources(qdevicewatcher PRIVATE
      qdevicewatcher/src/qdevicewatcher_win32.cpp
    )
elseif(APPLE)
    target_sources(qdevicewatcher PRIVATE
      qdevicewatcher/src/qdevicewatcher_mac.cpp
    )
elseif(UNIX)
    target_sources(qdevicewatcher PRIVATE
      qdevicewatcher/src/qdevicewatcher_linux.cpp
    )
endif()
