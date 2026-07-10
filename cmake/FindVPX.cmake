
set(_VPX_ROOT_HINTS)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
	list(APPEND _VPX_ROOT_HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
	list(APPEND _VPX_ROOT_HINTS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
if(DEFINED ENV{VPXDIR})
	list(APPEND _VPX_ROOT_HINTS "$ENV{VPXDIR}")
endif()

# vcpkg's libvpx config locates its archive with find_library(). During a
# Linux-hosted MinGW build our chainload toolchain correctly restricts normal
# library searches to the MinGW sysroot, which causes that config to re-root
# its own absolute vcpkg path and fail before this module can use its fallback.
# Resolve the manifest installation explicitly in this one cross-build case.
set(_VPX_VCPKG_MINGW FALSE)
if(MINGW AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "mingw")
	set(_VPX_VCPKG_MINGW TRUE)
endif()

if(_VPX_VCPKG_MINGW)
	find_path(VPX_INCLUDE_DIR
		NAMES vpx/vp8dx.h vpx/vpx_decoder.h
		PATHS ${_VPX_ROOT_HINTS}
		PATH_SUFFIXES include
		NO_DEFAULT_PATH
		NO_CMAKE_FIND_ROOT_PATH)
	find_library(VPX_LIBRARIES
		NAMES vpx libvpx
		PATHS ${_VPX_ROOT_HINTS}
		PATH_SUFFIXES lib
		NO_DEFAULT_PATH
		NO_CMAKE_FIND_ROOT_PATH)
else()
	find_package(unofficial-libvpx CONFIG QUIET)

	if(TARGET unofficial::libvpx::libvpx)
		set(VPX_LIBRARIES unofficial::libvpx::libvpx)
		get_target_property(VPX_INCLUDE_DIR unofficial::libvpx::libvpx INTERFACE_INCLUDE_DIRECTORIES)
	endif()

	if(NOT VPX_LIBRARIES OR NOT VPX_INCLUDE_DIR)
		find_package(PkgConfig QUIET)
		if(PkgConfig_FOUND)
			pkg_check_modules(PC_VPX QUIET vpx)
		endif()

		find_path(VPX_INCLUDE_DIR
			NAMES vpx/vp8dx.h vpx/vpx_decoder.h
			HINTS ${_VPX_ROOT_HINTS} ${PC_VPX_INCLUDE_DIRS}
			PATH_SUFFIXES include)

		find_library(VPX_LIBRARIES
			NAMES vpx libvpx
			HINTS ${_VPX_ROOT_HINTS} ${PC_VPX_LIBRARY_DIRS}
			PATH_SUFFIXES lib)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VPX DEFAULT_MSG VPX_LIBRARIES VPX_INCLUDE_DIR)

mark_as_advanced(VPX_INCLUDE_DIR VPX_LIBRARIES)
