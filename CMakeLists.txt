cmake_minimum_required(VERSION 3.24)

project(Ultraview
    VERSION 0.3.1
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
option(ULTRAVIEW_REQUIRE_WAYLAND_SHORTCUT_INHIBIT
    "Fail the build when Wayland keyboard-shortcut inhibition is unavailable" OFF)
option(ULTRAVIEW_REQUIRE_AUDIO
    "Fail the build when remote-audio playback (PipeWire) is unavailable" OFF)
option(ULTRAVIEW_REQUIRE_WAYLAND_HOST
    "Fail the build when host-mode Wayland portal capture is unavailable" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
find_package(Qt6 QUIET COMPONENTS DBus)
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
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)

# Wayland's keyboard-shortcuts-inhibit protocol lets an active remote-control
# session receive Super/Meta and other compositor shortcuts instead of triggering
# actions on the controller machine. It relies on QtGui's private Wayland window
# surface API (already used for KDE global-menu support) and on libwayland-client.
# The generated protocol bindings are kept in source, so wayland-scanner is not a
# build dependency.
set(ULTRAVIEW_WAYLAND_SHORTCUT_INHIBIT OFF)
if(ULTRAVIEW_GLOBAL_MENU AND WAYLAND_CLIENT_FOUND)
    set(ULTRAVIEW_WAYLAND_SHORTCUT_INHIBIT ON)
endif()
if(ULTRAVIEW_WAYLAND_SHORTCUT_INHIBIT)
    message(STATUS "Wayland shortcut inhibition: enabled")
else()
    message(STATUS "Wayland shortcut inhibition: disabled (needs QtGui private API and libwayland-client)")
    if(ULTRAVIEW_REQUIRE_WAYLAND_SHORTCUT_INHIBIT)
        message(FATAL_ERROR "ULTRAVIEW_REQUIRE_WAYLAND_SHORTCUT_INHIBIT is ON but the feature is unavailable")
    endif()
endif()

# Wayland host capture/input: xdg-desktop-portal RemoteDesktop + ScreenCast over
# QtDBus, frames through PipeWire.  The feature is optional so the controller
# keeps building on systems without the PipeWire development headers.
set(ULTRAVIEW_WAYLAND_HOST OFF)
if(TARGET Qt6::DBus AND PIPEWIRE_FOUND)
    set(ULTRAVIEW_WAYLAND_HOST ON)
    message(STATUS "Wayland host capture: enabled (QtDBus + libpipewire-0.3)")
else()
    message(STATUS "Wayland host capture: disabled (needs Qt6::DBus and libpipewire-0.3)")
    if(ULTRAVIEW_REQUIRE_WAYLAND_HOST)
        message(FATAL_ERROR "ULTRAVIEW_REQUIRE_WAYLAND_HOST is ON but Wayland host capture is unavailable")
    endif()
endif()

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
    src/core/Language.cpp
    src/core/SessionManager.cpp

    src/protocol/AnsiCodec.cpp
    src/protocol/Digest.cpp
    src/protocol/PacketCodec.cpp
    src/protocol/RegistrationCodec.cpp
    src/protocol/AuthenticationCodec.cpp
    src/protocol/RouteParser.cpp
    src/protocol/InputCodec.cpp
    src/protocol/P2pCodec.cpp
    src/protocol/ZstdCodec.cpp
    src/protocol/ClipboardCodec.cpp
    src/protocol/ControlCommands.cpp
    src/protocol/FileTransferCodec.cpp

    src/audio/G722Decoder.cpp
    src/audio/AudioPlayer.cpp
    src/audio/AudioSink.cpp

    src/host/HostProfile.cpp
    src/host/HostProtocol.cpp
    src/host/HostSession.cpp
    src/host/HostManager.cpp
    src/host/HostBackend.cpp
    src/host/InputMapping.cpp

    src/decoder/ZstdDecoder.cpp
    src/decoder/JpegAtlasDecoder.cpp
    src/decoder/TileCache.cpp
    src/decoder/FrameBuffer.cpp
    src/decoder/ScreenDecoder.cpp
    src/decoder/CursorDecoder.cpp
    src/decoder/CursorCache.cpp

    src/network/HttpTunnelSocket.cpp
    src/network/RegistrationTransport.cpp
    src/network/RelayTransport.cpp
    src/network/ConnectionManager.cpp

    src/input/KeyboardMapper.cpp
    src/input/InputController.cpp

    src/transfer/FileTransferService.cpp

    src/render/ViewportMap.cpp
    src/render/RemoteScreenItem.cpp

    src/security/TlsPolicy.cpp
    src/security/CredentialStore.cpp

    src/models/ProfileLoader.cpp
    src/models/ConnectionViewModel.cpp
    src/models/HostViewModel.cpp
    src/models/SettingsModel.cpp
    src/models/RecentConnectionsModel.cpp

    src/platform/GlobalMenu.cpp
    src/platform/ClipboardBridge.cpp
)
add_library(Ultraview::Core ALIAS ultraview_core)

qt_add_resources(ultraview_core builtin_dictionary
    PREFIX /ultraview
    BASE fromWindows
    FILES fromWindows/8bit.dat)

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
if(ULTRAVIEW_WAYLAND_SHORTCUT_INHIBIT)
    target_sources(ultraview_core PRIVATE
        src/platform/WaylandShortcutInhibitor.cpp
        src/platform/wayland/KeyboardShortcutsInhibitProtocol.cpp
    )
    # wayland-scanner emits an ELF visibility attribute on protocol interface
    # constants. GCC ignores it when this generated C source is compiled as C++.
    set_source_files_properties(src/platform/wayland/KeyboardShortcutsInhibitProtocol.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-attributes")
    target_link_libraries(ultraview_core PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(ultraview_core PRIVATE ULTRAVIEW_HAVE_WAYLAND_SHORTCUT_INHIBIT=1)
endif()

# Remote computer sound (command 43).  The decoder is always built; the PipeWire
# sink is optional so the client keeps building on systems whose PipeWire
# development headers are absent.
set(ULTRAVIEW_PIPEWIRE_AUDIO OFF)
if(PIPEWIRE_FOUND)
    set(ULTRAVIEW_PIPEWIRE_AUDIO ON)
    message(STATUS "Remote audio output: enabled (PipeWire playback sink)")
else()
    message(STATUS "Remote audio output: disabled (needs libpipewire-0.3); audio is decoded and dropped")
    if(ULTRAVIEW_REQUIRE_AUDIO)
        message(FATAL_ERROR "ULTRAVIEW_REQUIRE_AUDIO is ON but libpipewire-0.3 was not found")
    endif()
endif()
if(ULTRAVIEW_PIPEWIRE_AUDIO)
    target_sources(ultraview_core PRIVATE
        src/audio/PipeWireAudioSink.cpp
    )
    target_link_libraries(ultraview_core PRIVATE PkgConfig::PIPEWIRE)
    target_compile_definitions(ultraview_core PRIVATE ULTRAVIEW_HAVE_PIPEWIRE_AUDIO=1)
endif()

if(ULTRAVIEW_WAYLAND_HOST)
    target_sources(ultraview_core PRIVATE
        src/host/WaylandPortal.cpp
    )
    target_link_libraries(ultraview_core PRIVATE Qt6::DBus PkgConfig::PIPEWIRE)
    target_compile_definitions(ultraview_core PRIVATE ULTRAVIEW_HAVE_WAYLAND_HOST=1)
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
        qml/components/ChatPanel.qml
        qml/components/CountBadge.qml
        qml/components/EmptyState.qml
        qml/components/FilePicker.qml
        qml/components/IconButton.qml
        qml/components/PasswordField.qml
        qml/components/StatRow.qml
        qml/components/StatusBadge.qml
        qml/components/ToolsPanel.qml
        qml/components/TransferPanel.qml
        qml/components/ViewerToolbar.qml
        qml/pages/ConnectPage.qml
        qml/pages/HostPage.qml
        qml/pages/ViewerPage.qml
        qml/dialogs/AboutDialog.qml
        qml/dialogs/HostApprovalDialog.qml
        qml/dialogs/LogDialog.qml
        qml/dialogs/SettingsDialog.qml
        qml/themes/Icon.qml
        qml/themes/Theme.qml
    RESOURCES
        qml/assets/icons/chart.svg
        qml/assets/icons/chat.svg
        qml/assets/icons/check.svg
        qml/assets/icons/chevron-down.svg
        qml/assets/icons/chevron-up.svg
        qml/assets/icons/close.svg
        qml/assets/icons/eye-off.svg
        qml/assets/icons/eye.svg
        qml/assets/icons/folder.svg
        qml/assets/icons/gear.svg
        qml/assets/icons/info.svg
        qml/assets/icons/ultraview.svg
        qml/assets/icons/window-minimize.svg
        qml/assets/icons/window-maximize.svg
        qml/assets/icons/window-restore.svg
        qml/assets/icons/keyboard.svg
        qml/assets/icons/monitor.svg
        qml/assets/icons/power.svg
        qml/assets/icons/volume.svg
        qml/assets/icons/volume-off.svg
)

qt_add_resources(ultraview translations
    PREFIX /i18n
    BASE translations
    FILES translations/ultraview_vi.qm)

target_link_libraries(ultraview PRIVATE Ultraview::Core Qt6::Quick Qt6::QuickControls2 Qt6::Qml)
if(ULTRAVIEW_WAYLAND_SHORTCUT_INHIBIT)
    # main.cpp owns the window-bound inhibitor, so it needs the same feature
    # definition as the core object that implements it.
    target_compile_definitions(ultraview PRIVATE ULTRAVIEW_HAVE_WAYLAND_SHORTCUT_INHIBIT=1)
endif()

set_target_properties(ultraview PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
    OUTPUT_NAME ultraview)

install(TARGETS ultraview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/ultraview.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES qml/assets/icons/ultraview.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Debian policy expects the documentation under `share/doc/<package name>` and a
# `copyright` file with the license text; the TGZ package gets the same layout.
set(ULTRAVIEW_INSTALL_DOCDIR "${CMAKE_INSTALL_DATADIR}/doc/ultraview")
install(FILES README.md NOTICE.md DESTINATION ${ULTRAVIEW_INSTALL_DOCDIR})
install(FILES LICENSE DESTINATION ${ULTRAVIEW_INSTALL_DOCDIR} RENAME copyright)
install(DIRECTORY docs/ DESTINATION ${ULTRAVIEW_INSTALL_DOCDIR}/docs FILES_MATCHING PATTERN "*.md")
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
