# Build .deb packages against the distribution's Qt, not a private SDK.
set(CPACK_PACKAGE_NAME "ultraview")
set(CPACK_PACKAGE_VENDOR "Independent Ultraview project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Experimental native UltraViewer-compatible controller")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Ultraview maintainers (local build)")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# QML plugins are loaded dynamically, hence not found by dpkg-shlibdeps.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "ca-certificates, qt6-qpa-plugins, qt6-image-formats-plugins, qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qt-labs-folderlistmodel, qml6-module-qtqml, qml6-module-qtqml-models, qml6-module-qtqml-workerscript")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
include(CPack)
