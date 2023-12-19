if (MADRONA_TOOLCHAIN_ROOT_OVERRIDE)
    set(TOOLCHAIN_ROOT "${MADRONA_TOOLCHAIN_ROOT_OVERRIDE}")
else()
    set(TOOLCHAIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/bundled-toolchain")
endif()

set(TOOLCHAIN_SYSROOT "${TOOLCHAIN_ROOT}/toolchain")

if (APPLE)
    file(GLOB TOOLCHAIN_SYSROOT "${TOOLCHAIN_SYSROOT}/Toolchains/LLVM*.xctoolchain/usr")
endif()

find_library(MADRONA_BUNDLED_LIBCXX libc++-noexcept.so.1 libc++-noexcept.1.dylib libc++-noexcept.dll
    PATHS "${TOOLCHAIN_ROOT}/libcxx-noexcept/lib"
    REQUIRED
    NO_DEFAULT_PATH
)

cmake_path(GET MADRONA_BUNDLED_LIBCXX FILENAME MADRONA_LIBCXX_NAME)
set(MADRONA_LIBCXX_BUILDLOC "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${MADRONA_LIBCXX_NAME}")

add_custom_command(
    OUTPUT ${MADRONA_LIBCXX_BUILDLOC}
    DEPENDS ${MADRONA_BUNDLED_LIBCXX}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MADRONA_BUNDLED_LIBCXX}  ${MADRONA_LIBCXX_BUILDLOC}
)

add_custom_target(madrona_libcxx_shlib
    DEPENDS ${MADRONA_LIBCXX_BUILDLOC}
)

add_library(madrona_libcxx_shared SHARED IMPORTED GLOBAL)
add_dependencies(madrona_libcxx_shared madrona_libcxx_shlib)
set_target_properties(madrona_libcxx_shared PROPERTIES IMPORTED_LOCATION ${MADRONA_LIBCXX_BUILDLOC})

target_compile_options(madrona_libcxx_shared INTERFACE
    -nostdinc++ -nostdlib++
)
target_link_options(madrona_libcxx_shared INTERFACE
    -nostdlib++
)
target_include_directories(madrona_libcxx_shared SYSTEM INTERFACE
    $<BUILD_INTERFACE:${TOOLCHAIN_ROOT}/libcxx-noexcept/include/c++/v1>
)

add_library(madrona_libcxx INTERFACE)

target_compile_options(madrona_libcxx INTERFACE
    -nostdinc++ -nostdlib++
)

target_include_directories(madrona_libcxx SYSTEM INTERFACE
    $<BUILD_INTERFACE:${TOOLCHAIN_ROOT}/libcxx-noexcept/include/c++/v1>
)

target_link_libraries(madrona_libcxx INTERFACE
    "${TOOLCHAIN_ROOT}/libcxx-noexcept/lib/libc++-noexcept.a"
)

target_link_options(madrona_libcxx INTERFACE
    -nostdlib++
)

add_library(madrona_libcxx_hdrs INTERFACE)

target_compile_options(madrona_libcxx_hdrs INTERFACE
    -nostdinc++ -nostdlib++
)

target_include_directories(madrona_libcxx_hdrs SYSTEM INTERFACE
    $<BUILD_INTERFACE:${TOOLCHAIN_ROOT}/libcxx-noexcept/include/c++/v1>
)

add_library(madrona_libcxx_except INTERFACE)

target_compile_options(madrona_libcxx_except INTERFACE
    -nostdlib++
)

find_library(MADRONA_STATIC_LIBCXX_EXCEPT libc++.a
    PATHS "${TOOLCHAIN_SYSROOT}/lib"
    PATH_SUFFIXES "x86_64-pc-linux-gnu"
    REQUIRED
    NO_DEFAULT_PATH
)

target_link_libraries(madrona_libcxx_except INTERFACE
    "${MADRONA_STATIC_LIBCXX_EXCEPT}"
)

target_link_options(madrona_libcxx_except INTERFACE
    -nostdlib++
)

add_library(madrona_libcxx_except_hdrs INTERFACE)

target_compile_options(madrona_libcxx_except_hdrs INTERFACE
    -nostdlib++
)

install(TARGETS madrona_libcxx madrona_libcxx_except EXPORT madronaLibcxx)
install(EXPORT madronaLibcxx DESTINATION ${CMAKE_INSTALL_PREFIX})
