set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
if (POLICY CMP0071)
    cmake_policy(SET CMP0071 NEW)
endif()

file(GLOB_RECURSE THEMES ${PROJECT_SOURCE_DIR}/dist/themes/*)

add_executable(threeSD
  import_dialog.cpp
  import_dialog.h
  import_dialog.ui
  import_job.cpp
  import_job.h
  main.cpp
  main.h
  main.ui
  select_files_dialog.cpp
  select_files_dialog.h
  select_files_dialog.ui
  utilities.cpp
  utilities.h
  utilities.ui
  ${THEMES}
)

target_link_libraries(threeSD PRIVATE common core)
target_link_libraries(threeSD PRIVATE Qt5::Widgets)
target_link_libraries(threeSD PRIVATE qdevicewatcher)
target_link_libraries(threeSD PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if (APPLE)
    # TODO: support macOS
    # set(MACOSX_ICON "../../dist/citra.icns")
    # set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    # target_sources(threeSD PRIVATE ${MACOSX_ICON})
    # set_target_properties(threeSD PROPERTIES MACOSX_BUNDLE TRUE)
    # set_target_properties(threeSD PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)
elseif(WIN32)
    # compile as a win32 gui application instead of a console application
    target_link_libraries(threeSD PRIVATE Qt5::WinMain)
    if(MSVC)
        set_target_properties(threeSD PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(threeSD PROPERTIES LINK_FLAGS_RELEASE "-mwindows")
    endif()
endif()

target_compile_definitions(threeSD PRIVATE
    # Use QStringBuilder for string concatenation to reduce
    # the overall number of temporary strings created.
    -DQT_USE_QSTRINGBUILDER

    # Disable implicit type narrowing in signal/slot connect() calls.
    -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT

    # Disable unsafe overloads of QProcess' start() function.
    -DQT_NO_PROCESS_COMBINED_ARGUMENT_START

    # Disable implicit QString->QUrl conversions to enforce use of proper resolving functions.
    -DQT_NO_URL_CAST_FROM_STRING

    # Disable automatic conversions from 8-bit strings (char *) to unicode QStrings
    -DQT_NO_CAST_FROM_ASCII
)

if (MSVC)
    include(CopyQt5Deps)
    copy_Qt5_deps(threeSD)
endif()
