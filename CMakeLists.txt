cmake_minimum_required(VERSION 3.28)
project(network_os)
set(CMAKE_CXX_STANDARD 20)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/cpp_socket/include
    ${CMAKE_SOURCE_DIR}/external/cpp_utils/include
)

add_executable(init_debug ${CMAKE_SOURCE_DIR}/src/init.cpp)
add_executable(device_manager_debug ${CMAKE_SOURCE_DIR}/src/device_manager.cpp)
add_executable(forwarder_debug ${CMAKE_SOURCE_DIR}/src/forwarder.cpp)
target_compile_definitions(init_debug PUBLIC DEBUG=1)

# --- Link all executables below here statically ---
set(CMAKE_EXE_LINKER_FLAGS "-static")
add_executable(init ${CMAKE_SOURCE_DIR}/src/init.cpp)
add_executable(device_manager ${CMAKE_SOURCE_DIR}/src/device_manager.cpp)
add_executable(forwarder ${CMAKE_SOURCE_DIR}/src/forwarder.cpp)


# Force static linking only for init
# set_target_properties(init PROPERTIES
#     LINK_FLAGS "-static"
# )
target_compile_definitions(init PUBLIC DEBUG=0)

# Output archive
set(INITRAMFS_OUT ${CMAKE_BINARY_DIR}/initramfs.cpio.gz)

# Staging root for the initramfs
set(INITRAMFS_ROOT ${CMAKE_BINARY_DIR}/initramfs_root)

# ---- Stage the initramfs tree ------------------------------------------------
add_custom_command(
  OUTPUT ${INITRAMFS_ROOT}/.staged
  DEPENDS init
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${INITRAMFS_ROOT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/bin
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/proc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/sys
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/dev

  # Place the compiled binary as /init
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init> ${INITRAMFS_ROOT}/init
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:device_manager> ${INITRAMFS_ROOT}/bin/device_manager
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:forwarder> ${INITRAMFS_ROOT}/bin/forwarder

  COMMAND ${CMAKE_COMMAND} -E touch ${INITRAMFS_ROOT}/.staged
  COMMENT "Staging initramfs root at ${INITRAMFS_ROOT}"
)

add_custom_target(stage_initramfs ALL DEPENDS ${INITRAMFS_ROOT}/.staged)

# ---- Pack initramfs.cpio.gz --------------------------------------------------
set(_initramfs_tmp "${CMAKE_CURRENT_BINARY_DIR}/_initramfs_tmp")

add_custom_command(
  OUTPUT ${INITRAMFS_OUT}
  DEPENDS stage_initramfs
  WORKING_DIRECTORY ${INITRAMFS_ROOT}

  COMMAND ${CMAKE_COMMAND} -E remove_directory "${_initramfs_tmp}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_initramfs_tmp}"

  COMMAND find . -name .staged -prune -o -print0 > "${_initramfs_tmp}/filelist.txt"

  COMMAND cpio --null -ov --format=newc --quiet < "${_initramfs_tmp}/filelist.txt" > "${_initramfs_tmp}/initramfs.cpio"
  COMMAND gzip -9 -c "${_initramfs_tmp}/initramfs.cpio" > "${INITRAMFS_OUT}"

  COMMAND ${CMAKE_COMMAND} -E remove_directory "${_initramfs_tmp}"
  COMMENT "Creating ${INITRAMFS_OUT}"
)

add_custom_target(create_initramfs ALL DEPENDS ${INITRAMFS_OUT})

# Convenience message
add_custom_target(build_initramfs ALL
  COMMAND ${CMAKE_COMMAND} -E echo "Initramfs: ${INITRAMFS_OUT}"
  COMMAND ${CMAKE_COMMAND} -E echo "Root dir:  ${INITRAMFS_ROOT}"
)
add_dependencies(build_initramfs create_initramfs)
