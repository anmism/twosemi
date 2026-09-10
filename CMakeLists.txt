cmake_minimum_required(VERSION 3.25)

project(TwoSemi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(TwoSemi WIN32
    src/main.cpp
)

target_compile_features(TwoSemi PRIVATE cxx_std_20)

target_compile_definitions(TwoSemi PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if (MSVC)
    target_compile_options(TwoSemi PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

target_link_libraries(TwoSemi PRIVATE
    user32
    shell32
    advapi32
    crypt32
    ole32
    shlwapi
    gdiplus
    comdlg32
    winhttp
    winsqlite3
)

add_custom_command(TARGET TwoSemi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TwoSemi>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
            "$<TARGET_FILE_DIR:TwoSemi>/assets/icon.png"
)

install(TARGETS TwoSemi
    RUNTIME DESTINATION .
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    DESTINATION assets
)

set(CPACK_PACKAGE_NAME "TwoSemi")
set(CPACK_PACKAGE_VENDOR "TwoSemi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Keyboard-first notes, reminders, focus, and AI chat")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TwoSemi")
set(CPACK_PACKAGE_FILE_NAME "TwoSemi-${PROJECT_VERSION}-win64")
set(CPACK_GENERATOR "ZIP;NSIS")

set(CPACK_NSIS_DISPLAY_NAME "TwoSemi")
set(CPACK_NSIS_PACKAGE_NAME "TwoSemi")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_MENU_LINKS
    "TwoSemi.exe" "TwoSemi"
)
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP/TwoSemi.lnk' '$INSTDIR/TwoSemi.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$DESKTOP/TwoSemi.lnk'"
)

include(CPack)
