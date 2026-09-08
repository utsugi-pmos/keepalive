# Two front-ends over one backend:
#
#   kcm_keepalive.so  the Settings module, inside Preferences
#   keepalive         a normal application, in the app drawer
#
# The app is not a duplicate. A KCM is a plugin loaded into plasma-settings, so
# it is tied to the KF6 ABI and an update can leave it unable to load --
# kcm_lookandfeel.so on this device is already in that state ("symbol not
# found" in the session log). The app needs only Qt and Kirigami, so the
# settings stay reachable when that happens.
#
# Both link the same backend.cpp and show the same AppList.qml, so they cannot
# drift apart: there is one implementation and two frames around it.
cmake_minimum_required(VERSION 3.20)
project(keepalive VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(KF_MIN_VERSION 6.0)
set(QT_MIN_VERSION 6.6)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
	CoreAddons
	Config
	I18n
	KCMUtils
	Service
)

qt_standard_project_setup(REQUIRES 6.6)

# --- the shared backend ------------------------------------------------------
# A static library so both front-ends carry the same code without one depending
# on the other: the app must keep working even if the KCM cannot be loaded.
add_library(keepalive_backend STATIC backend.cpp backend.h)

target_include_directories(keepalive_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Position-independent code: it gets linked into kcm_keepalive.so.
set_target_properties(keepalive_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Qt::DBus is not decoration: writing the list is only half the job. The task
# switcher reads its config once and caches it, so the effect has to be reloaded
# over D-Bus, and KWin has to be told to re-read the window rules. Measured, see
# backend.cpp.
target_link_libraries(keepalive_backend PUBLIC
	Qt::Core
	Qt::DBus
	KF6::ConfigCore
	KF6::CoreAddons
	KF6::Service
)

# --- the Settings module -----------------------------------------------------
kcmutils_add_qml_kcm(kcm_keepalive SOURCES kcm.cpp)

target_link_libraries(kcm_keepalive PRIVATE
	keepalive_backend
	KF6::I18n
	KF6::KCMUtils
	KF6::KCMUtilsQuick
)

# --- the standalone application ----------------------------------------------
qt_add_executable(keepalive main.cpp)

# The list is shared, but a single .qml file cannot belong to two QML modules:
# qt_add_qml_module stores the URI in source file properties, so the second
# module would silently overwrite the first one's. And kcmutils_add_qml_kcm
# GLOB_RECURSEs the whole ui/ directory, so anything left there is the KCM's.
#
# Hence a copy for the app, made at build time. The source of truth stays
# ui/AppList.qml -- there is still only one file to edit.
configure_file(ui/AppList.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/AppList.qml COPYONLY)
configure_file(App.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml COPYONLY)

# Generated files carry an absolute path, and Qt refuses to put those in a
# resource without being told what to call them inside it. Without this the
# types would not be reachable as KeepAlive.App / KeepAlive.AppList.
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
	PROPERTIES QT_RESOURCE_ALIAS App.qml)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/AppList.qml
	PROPERTIES QT_RESOURCE_ALIAS AppList.qml)

qt_add_qml_module(keepalive
	URI KeepAlive
	VERSION 1.0
	QML_FILES
		${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
		${CMAKE_CURRENT_BINARY_DIR}/appqml/AppList.qml
)

target_link_libraries(keepalive PRIVATE
	keepalive_backend
	Qt::Gui
	Qt::Qml
	Qt::Quick
	Qt::QuickControls2
	Qt::Widgets
	KF6::CoreAddons
	KF6::I18n
)

include(GNUInstallDirs)
install(TARGETS keepalive
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES org.utsugi.keepalive.desktop
	DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES org.utsugi.keepalive.svg
	DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
