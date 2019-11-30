cmake_minimum_required(VERSION 3.9)
project(wrt-libppdemo)

#[===========================================[
  this pulls-in the rmcd source code via
  git submodule. This demo exe depends on the
  xrpl_core library which is defined by the
  rmcd project.
#]===========================================]
if (MSVC)
  #[=======================================[
    HACK to patch a one-line windows-only
    bug in rmcd CMakeLists.txt. This can
    be removed once rmcd 1.2 is available
    and we update the submodule
  #]=======================================]
  execute_process( COMMAND powershell -Command
     "(Get-Content extras/wrtd/CMakeLists.txt) \
     -replace '^exclude_if_included\\s+\\(rocksdb\\)', '##' \
     | Set-Content extras/wrtd/CMakeLists.txt"
     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif ()
add_subdirectory(extras/wrtd)

add_executable(wrtlibppdemo src/test/wrt-libpp_demo.cpp)
target_link_libraries (wrtlibppdemo Ripple::xrpl_core)
