cmake_minimum_required(VERSION 3.28)
project(network_os)
set(CMAKE_CXX_STANDARD 20)

# --- Build the init binary (statically linked) ---
add_executable(init ${CMAKE_SOURCE_DIR}/src/main.cpp)

# Force static linking only for init
set_target_properties(init PROPERTIES
    LINK_FLAGS "-static"
)

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

add_custom_target(initramfs ALL DEPENDS ${INITRAMFS_OUT})

# Convenience message
add_custom_target(print_initramfs_info ALL
  COMMAND ${CMAKE_COMMAND} -E echo "Initramfs: ${INITRAMFS_OUT}"
  COMMAND ${CMAKE_COMMAND} -E echo "Root dir:  ${INITRAMFS_ROOT}"
)
add_dependencies(print_initramfs_info initramfs)
