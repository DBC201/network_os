cmake_minimum_required(VERSION 3.28)
project(network_os)
set(CMAKE_CXX_STANDARD 20)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/cpp_socket/include
    ${CMAKE_SOURCE_DIR}/external/cpp_utils/include
)

add_executable(init_debug ${CMAKE_SOURCE_DIR}/src/init.cpp)
target_compile_definitions(init_debug PUBLIC DEBUG=1)

# add_custom_target(all_debug
#     DEPENDS init_debug device_manager_debug forwarder_debug
# )

# --- Link all executables below here statically ---
set(CMAKE_EXE_LINKER_FLAGS "-static")
add_executable(init ${CMAKE_SOURCE_DIR}/src/init.cpp)
add_executable(device_manager ${CMAKE_SOURCE_DIR}/src/device_manager.cpp)
add_executable(forwarder ${CMAKE_SOURCE_DIR}/src/forwarder.cpp)
add_executable(pids ${CMAKE_SOURCE_DIR}/src/pids.cpp)
add_executable(shell ${CMAKE_SOURCE_DIR}/src/shell.cpp)
add_executable(interface ${CMAKE_SOURCE_DIR}/src/interface.cpp)

add_custom_target(
    build_os_programs DEPENDS init device_manager forwarder pids shell
)

# Force static linking only for init
# set_target_properties(init PROPERTIES
#     LINK_FLAGS "-static"
# )

# Output archive
set(INITRAMFS_OUT ${CMAKE_BINARY_DIR}/initramfs.cpio.gz)

# Staging root for the initramfs
set(INITRAMFS_ROOT ${CMAKE_BINARY_DIR}/initramfs_root)

# ---- Stage the initramfs tree ------------------------------------------------
add_custom_command(
  OUTPUT ${INITRAMFS_ROOT}/.staged
  DEPENDS build_os_programs
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/bin
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/proc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/sys
  COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_ROOT}/dev

  # Place the compiled binary as /init
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init> ${INITRAMFS_ROOT}/init
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:device_manager> ${INITRAMFS_ROOT}/bin/device_manager
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:forwarder> ${INITRAMFS_ROOT}/bin/forwarder
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:pids> ${INITRAMFS_ROOT}/bin/pids
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:shell> ${INITRAMFS_ROOT}/bin/shell
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:interface> ${INITRAMFS_ROOT}/bin/interface

  COMMAND ${CMAKE_COMMAND} -E touch ${INITRAMFS_ROOT}/.staged
  COMMENT "Staging initramfs root at ${INITRAMFS_ROOT}"
)

# ---- Pack initramfs.cpio.gz --------------------------------------------------
set(_initramfs_tmp "${CMAKE_CURRENT_BINARY_DIR}/_initramfs_tmp")

add_custom_command(
  OUTPUT ${INITRAMFS_OUT}
  DEPENDS ${INITRAMFS_ROOT}/.staged
  WORKING_DIRECTORY ${INITRAMFS_ROOT}

  COMMAND ${CMAKE_COMMAND} -E remove_directory "${_initramfs_tmp}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_initramfs_tmp}"

  COMMAND find . -name .staged -prune -o -print0 > "${_initramfs_tmp}/filelist.txt"

  COMMAND cpio --null -ov --format=newc --quiet < "${_initramfs_tmp}/filelist.txt" > "${_initramfs_tmp}/initramfs.cpio"
  COMMAND gzip -9 -c "${_initramfs_tmp}/initramfs.cpio" > "${INITRAMFS_OUT}"

  COMMAND ${CMAKE_COMMAND} -E remove_directory "${_initramfs_tmp}"

  COMMAND ${CMAKE_COMMAND} -E echo "Initramfs: ${INITRAMFS_OUT}"
  COMMAND ${CMAKE_COMMAND} -E echo "Root dir:  ${INITRAMFS_ROOT}"
  COMMAND rm ${INITRAMFS_ROOT}/.staged
)

add_custom_target(pack_initramfs ALL DEPENDS ${INITRAMFS_OUT})
