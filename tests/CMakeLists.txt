#
# Test suite: byte-level parity tests against the fixtures generated from the
# Python reference, plus an end-to-end session test against the offline mock
# server in TestSupport.cpp and the app's offscreen self-test.
#
set(UV_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(UV_TEST_FIXTURES "${UV_TEST_DATA_DIR}/fixtures.json")

set(UV_TEST_SOURCES
    tst_packet_codec
    tst_registration
    tst_auth
    tst_route
    tst_profiles
    tst_keyboard_mapper
    tst_screen_decode
    tst_viewport
    tst_render_item
    tst_session
    tst_globalmenu
    tst_host
    tst_clipboard
    tst_cursor
    tst_transfer
    tst_g722
)

foreach(test IN LISTS UV_TEST_SOURCES)
    add_executable(${test} ${test}.cpp TestSupport.cpp)
    target_link_libraries(${test} PRIVATE Ultraview::Core Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(${test} PRIVATE UV_TEST_FIXTURES="${UV_TEST_FIXTURES}")
    set_target_properties(${test} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES
        # No display is required; the render item test still needs a QGuiApplication.
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180)
endforeach()

# tst_globalmenu inspects the exported com.canonical.dbusmenu object over the
# session bus, which needs QtDBus; without it the test only checks the layout and
# the action handling.
find_package(Qt6 QUIET COMPONENTS DBus)
if(TARGET Qt6::DBus)
    target_link_libraries(tst_globalmenu PRIVATE Qt6::DBus)
    target_compile_definitions(tst_globalmenu PRIVATE ULTRAVIEW_TEST_DBUS=1)
endif()

# The application self-test drives the real QML scene and scene graph through the
# offscreen platform plugin. Worker integration is tested separately in tst_session.
add_test(NAME app_self_test COMMAND ultraview --self-test --mock-frames 3)
set_tests_properties(app_self_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)

# The live-connect probe must refuse before any network I/O when the client has no
# registration profile: it is a probe, not an automatic session.  A throw-away
# configuration directory keeps it away from the developer's own profile; the exit
# code is non-zero there, which PASS_REGULAR_EXPRESSION deliberately ignores.
add_test(NAME app_connect_probe_unconfigured COMMAND ultraview --connect-attempt 999999999)
set_tests_properties(app_connect_probe_unconfigured PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/connect-probe-config"
    PASS_REGULAR_EXPRESSION "connect-attempt: cannot start"
    TIMEOUT 60)

# A desktop theme can hand out a D-Bus menu bar even on a platform that cannot
# announce it to a window manager (the session bus offers the AppMenu registrar,
# the offscreen plugin has no desktop services).  Starting under such a theme
# must not crash and must simply leave the application without a global menu.
add_test(NAME app_self_test_themed
    COMMAND ultraview --self-test --mock-frames 1)
set_tests_properties(app_self_test_themed PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=kde"
    TIMEOUT 180)
