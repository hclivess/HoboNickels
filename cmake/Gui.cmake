# Qt5 wallet GUI (HoboNickels-qt).
#
# Included from the top-level CMakeLists.txt when -DWITH_QT_GUI=ON.  The GUI is
# a separate compilation of the core sources (defined with QT_GUI) plus the
# src/qt/* sources; it therefore cannot reuse the daemon's hobonickels_core
# object library (which is built without QT_GUI).

find_package(Qt5 5.9 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt5 QUIET COMPONENTS DBus LinguistTools)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# NOTE: AUTOUIC is intentionally OFF. The project ships its own non-uic header
# src/ui_interface.h, whose name matches AUTOUIC's "ui_*.h" detection and makes
# it look for a bogus "interface.ui". Drive uic explicitly instead.
file(GLOB UI_FORMS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/forms/*.ui)
qt5_wrap_ui(UI_HEADERS ${UI_FORMS})

# GUI-side .cpp: the src/qt/* widgets/models plus two wallet views that live
# in src/.  qrcodedialog.cpp is gated behind USE_QRCODE (libqrencode), and the
# Objective-C++ mac shims / Qt test harness are excluded on this build.
file(GLOB QT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/*.cpp)
list(REMOVE_ITEM QT_SOURCES
     # gated behind USE_QRCODE (libqrencode)
     ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qrcodedialog.cpp
     # dead code: not in the original .pro, header/impl out of sync, and the
     # .ui uses a plain QComboBox (not this promoted class).
     ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/qcomboboxfiltercoins.cpp)

set(GUI_SOURCES
    ${CORE_SOURCES}
    src/init.cpp
    src/noui.cpp
    src/walletview.cpp
    src/walletstack.cpp
    ${QT_SOURCES}
    ${UI_HEADERS}
    src/qt/bitcoin.qrc)

option(USE_QRCODE "Build the GUI with QR-code support (libqrencode)" OFF)
if(USE_QRCODE)
    find_library(QRENCODE_LIBRARY NAMES qrencode REQUIRED)
    list(APPEND GUI_SOURCES src/qt/qrcodedialog.cpp)
endif()

add_executable(HoboNickels-qt WIN32 ${GUI_SOURCES})
if(NOT USE_SYSTEM_LEVELDB)
    add_dependencies(HoboNickels-qt leveldb_ext)
endif()

target_include_directories(HoboNickels-qt PRIVATE
    ${COMMON_INCLUDE_DIRS}
    ${BDBXX_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qt/forms
    ${CMAKE_CURRENT_BINARY_DIR})   # generated ui_*.h from qt5_wrap_ui

target_compile_definitions(HoboNickels-qt PRIVATE ${COMMON_DEFINES} QT_GUI)

target_link_libraries(HoboNickels-qt PRIVATE
    leveldb
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network
    Boost::system Boost::filesystem Boost::program_options Boost::thread Boost::chrono
    OpenSSL::SSL OpenSSL::Crypto
    ${BDBXX_LIBRARY}
    Threads::Threads
    ${CMAKE_DL_LIBS})

if(Qt5DBus_FOUND)
    target_compile_definitions(HoboNickels-qt PRIVATE USE_DBUS)
    target_link_libraries(HoboNickels-qt PRIVATE Qt5::DBus)
endif()

if(WIN32)
    # The GUI is a separate compilation from hobonickels_core, so it must repeat
    # the Windows settings the daemon core has: define WIN32 (mingw only sets
    # _WIN32), winsock + shell/ole libs, and Qt5::WinMain to provide WinMain()
    # for the WIN32 (GUI subsystem) executable.
    target_compile_definitions(HoboNickels-qt PRIVATE
        WIN32 _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN)
    target_link_libraries(HoboNickels-qt PRIVATE
        Qt5::WinMain ws2_32 mswsock iphlpapi shlwapi shell32 ole32)
endif()

if(ENABLE_UPNP)
    target_compile_definitions(HoboNickels-qt PRIVATE USE_UPNP=1)
    target_include_directories(HoboNickels-qt PRIVATE ${MINIUPNPC_INCLUDE_DIR})
    target_link_libraries(HoboNickels-qt PRIVATE ${MINIUPNPC_LIBRARY})
endif()

if(USE_QRCODE)
    target_compile_definitions(HoboNickels-qt PRIVATE USE_QRCODE)
    target_link_libraries(HoboNickels-qt PRIVATE ${QRENCODE_LIBRARY})
endif()

install(TARGETS HoboNickels-qt RUNTIME DESTINATION bin)
