# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

if(EXISTS "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitclone-lastrun.txt" AND EXISTS "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitinfo.txt" AND
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitclone-lastrun.txt" IS_NEWER_THAN "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitinfo.txt")
  message(STATUS
    "Avoiding repeated git clone, stamp file is up to date: "
    "'/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitclone-lastrun.txt'"
  )
  return()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: '/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "/usr/bin/git"
            clone --no-checkout --config "advice.detachedHead=false" "https://github.com/ThePhD/sol2.git" "sol2-src"
    WORKING_DIRECTORY "/mnt/d/Projects/BreadTerminal/build-linux/_deps"
    RESULT_VARIABLE error_code
  )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(STATUS "Had to git clone more than once: ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/ThePhD/sol2.git'")
endif()

execute_process(
  COMMAND "/usr/bin/git"
          checkout "develop" --
  WORKING_DIRECTORY "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: 'develop'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "/usr/bin/git" 
            submodule update --recursive --init 
    WORKING_DIRECTORY "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src"
    RESULT_VARIABLE error_code
  )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: '/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitinfo.txt" "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
)
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: '/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/sol2-populate-gitclone-lastrun.txt'")
endif()
