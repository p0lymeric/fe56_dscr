cmake_minimum_required(VERSION 3.19)
project(c13 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets WebSockets Sql)

# enable use of qrc resource files
set(CMAKE_AUTORCC ON)

qt_standard_project_setup()

qt_add_executable(c13
    WIN32 MACOSX_BUNDLE
    main.cpp
    maincore.h maincore.cpp
    assets/assets.qrc

    dscrclient.h dscrclient.cpp

    database/database.h database/database.cpp
    database/dictionary.h database/dictionary.cpp
    database/settings.h database/settings.cpp
    database/wsmessagelog.h database/wsmessagelog.cpp

    mainwindow.h mainwindow.cpp mainwindow.ui
    serverconnectdialog.h serverconnectdialog.cpp serverconnectdialog.ui

)

target_link_libraries(c13
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::WebSockets
        Qt::Sql
)

target_include_directories(c13
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# TODO probably port symbolizer to C++
if(NOT DEFINED RUST_TARGET_PATH)
    target_link_directories(c13 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../rust/target/$<IF:$<CONFIG:Debug>,debug,release>")
else()
    target_link_directories(c13 PRIVATE "${RUST_TARGET_PATH}")
endif()

target_link_libraries(c13 PRIVATE fe56)
if(WIN32)
    target_link_libraries(c13 PRIVATE kernel32 ntdll userenv ws2_32 dbghelp)
endif()
target_include_directories(c13 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../rust/crates/fe56_capi/include)

install(TARGETS c13
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET c13
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
