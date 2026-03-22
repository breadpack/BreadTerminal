# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-src"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-build"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/tmp"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src"
  "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/d/Projects/BreadTerminal/build-linux/_deps/sol2-subbuild/sol2-populate-prefix/src/sol2-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
