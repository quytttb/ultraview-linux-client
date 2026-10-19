cmake_minimum_required(VERSION 3.24)

project(Ultraview
    VERSION 0.2.2
    DESCRIPTION "Native Linux remote desktop client for UltraViewer partners (independent re-implementation)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ULTRAVIEW_BUILD_TESTS "Build the test suite" ON)
option(ULTRAVIEW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ULTRAVIEW_ENABLE_SANITIZERS "Build with AddressSanitizer/UBSan (GCC/Clang only)" OFF)
option(ULTRAVIEW_REQUIRE_SECRET_SERVICE
    "Fail the build when a platform secret service (libsecret) is unavailable" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
find_package(OpenSSL 3.0 REQUIRED)

# ---------------------------------------------------------------------------
# KDE global menu (optional)
#
# Qt 6.9 moved the `com.canonical.dbusmenu` menu bar into QtGui's Unix platform
# theme (QGenericUnixTheme/QKdeTheme::createPlatformMenuBar).  Using it needs the
# private QtGui QPA headers, which only exist when the distribution ships them
# (`qt6-base-private-dev` on Debian/Ubuntu); the resulting binary then depends on
# `qt6-base-private-abi`, exactly like Plasma's own packages.  Without the
# headers the client builds unchanged and simply has no global menu.
# ---------------------------------------------------------------------------
set(ULTRAVIEW_GLOBAL_MENU OFF)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    if(TARGET Qt6::GuiPrivate)
        set(ULTRAVIEW_GLOBAL_MENU ON)
    endif()
endif()
if(ULTRAVIEW_GLOBAL_MENU)
    message(STATUS "KDE global menu support: enabled (QtGui private QPA menu API)")
else()
    message(STATUS "KDE global menu support: disabled (needs Qt >= 6.9 with Qt6::GuiPrivate)")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# optional platform secret storage (libsecret / GNOME keyring, KWallet via DBus)
# ---------------------------------------------------------------------------
pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
if(LIBSECRET_FOUND)
    message(STATUS "libsecret found: password persistence can use the platform keyring")
else()
    message(STATUS "libsecret not found: the client will not persist passwords (only partner IDs)")
    if(ULTRAVIEW_REQUIRE_SECRET_SERVICE)
        message(FATAL_ERROR "ULTRAVIEW_REQUIRE_SECRET_SERVICE is ON but libsecret-1 was not found")
    endif()
endif()

# ---------------------------------------------------------------------------
# core library: every piece of production logic. No Qt Widgets, no QML.
# ---------------------------------------------------------------------------
add_library(ultraview_core STATIC
    src/core/ErrorTypes.cpp
    src/core/SessionState.cpp
    src/core/SessionManager.cpp

    src/protocol/AnsiCodec.cpp
    src/protocol/Digest.cpp
    src/protocol/PacketCodec.cpp
    src/protocol/RegistrationCodec.cpp
    src/protocol/AuthenticationCodec.cpp
    src/protocol/RouteParser.cpp
    src/protocol/InputCodec.cpp
    src/protocol/P2pCodec.cpp

    src/decoder/ZstdDecoder.cpp
    src/decoder/JpegAtlasDecoder.cpp
    src/decoder/TileCache.cpp
    src/decoder/FrameBuffer.cpp
    src/decoder/ScreenDecoder.cpp

    src/network/HttpTunnelSocket.cpp
    src/network/RegistrationTransport.cpp
    src/network/RelayTransport.cpp
    src/network/ConnectionManager.cpp

    src/input/KeyboardMapper.cpp
    src/input/InputController.cpp

    src/render/ViewportMap.cpp
    src/render/RemoteScreenItem.cpp

    src/security/TlsPolicy.cpp
    src/security/CredentialStore.cpp

    src/models/ProfileLoader.cpp
    src/models/ConnectionViewModel.cpp
    src/models/SettingsModel.cpp
    src/models/RecentConnectionsModel.cpp

    src/platform/GlobalMenu.cpp
)
add_library(Ultraview::Core ALIAS ultraview_core)

target_include_directories(ultraview_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# zstd appears in the public header of ZstdDecoder, so consumers need it too;
# QtQuick appears in the public header of RemoteScreenItem.
target_link_libraries(ultraview_core
    PUBLIC Qt6::Core Qt6::Network Qt6::Gui Qt6::Quick PkgConfig::ZSTD
    PRIVATE OpenSSL::Crypto
)
target_compile_definitions(ultraview_core PUBLIC
    ULTRAVIEW_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${LIBSECRET_FOUND}>:ULTRAVIEW_HAVE_LIBSECRET=1>
)
if(LIBSECRET_FOUND)
    target_link_libraries(ultraview_core PRIVATE PkgConfig::LIBSECRET)
endif()
if(ULTRAVIEW_GLOBAL_MENU)
    target_link_libraries(ultraview_core PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(ultraview_core PRIVATE ULTRAVIEW_HAVE_GLOBAL_MENU=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ultraview_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
    if(ULTRAVIEW_WARNINGS_AS_ERRORS)
        target_compile_options(ultraview_core PRIVATE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# application: QML module + the QML-facing C++ types
#
# The QML types live in `ultraview_core` and are registered from main.cpp with
# qmlRegisterType() (so the same classes stay unit-testable); the QML module only
# carries the QML files.
# ---------------------------------------------------------------------------
qt_add_executable(ultraview
    src/app/main.cpp
)

set_source_files_properties(
    qml/themes/Icon.qml
    qml/themes/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ultraview
    URI Ultraview
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/AppButton.qml
        qml/components/AppCheckBox.qml
        qml/components/AppComboBox.qml
        qml/components/AppFieldBackground.qml
        qml/components/AppInlineField.qml
        qml/components/AppSpinner.qml
        qml/components/AppSpinBox.qml
        qml/components/AppTabButton.qml
        qml/components/AppTextField.qml
        qml/components/AppToolbar.qml
        qml/components/EmptyState.qml
        qml/components/FilePicker.qml
        qml/components/IconButton.qml
        qml/components/PasswordField.qml
        qml/components/StatRow.qml
        qml/components/StatusBadge.qml
        qml/components/ViewerToolbar.qml
        qml/pages/ConnectPage.qml
        qml/pages/ViewerPage.qml
        qml/dialogs/AboutDialog.qml
        qml/dialogs/SettingsDialog.qml
        qml/themes/Icon.qml
        qml/themes/Theme.qml
    RESOURCES
        qml/assets/icons/chart.svg
        qml/assets/icons/check.svg
        qml/assets/icons/chevron-down.svg
        qml/assets/icons/chevron-up.svg
        qml/assets/icons/close.svg
        qml/assets/icons/eye-off.svg
        qml/assets/icons/eye.svg
        qml/assets/icons/folder.svg
        qml/assets/icons/gear.svg
        qml/assets/icons/info.svg
        qml/assets/icons/keyboard.svg
        qml/assets/icons/monitor.svg
        qml/assets/icons/power.svg
)

target_link_libraries(ultraview PRIVATE Ultraview::Core Qt6::Quick Qt6::QuickControls2 Qt6::Qml)

set_target_properties(ultraview PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
    OUTPUT_NAME ultraview)

install(TARGETS ultraview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/ultraview.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/ultraview.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES README.md NOTICE.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs FILES_MATCHING PATTERN "*.md")
# Intentionally no install rule for reference/, tools/, tests/, or user profiles.
include(packaging/CPackConfig.cmake)

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING AND ULTRAVIEW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# sanitizer support (development only)
# ---------------------------------------------------------------------------
if(ULTRAVIEW_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target ultraview_core ultraview)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()
