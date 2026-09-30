# ---------------------------------------------------------------------------
#  Profile-guided optimisation, off by default
#
#  PINMAME_PGO=GEN builds the target instrumented: running it writes a profile
#  to PINMAME_PGO_DIR. PINMAME_PGO=USE builds it with that profile.
#    GCC, MinGW: -fprofile-generate / -fprofile-use; the profile is keyed by
#                object paths relative to the build directory, so another
#                build directory of the same tree can use it
#    Clang:      -fprofile-generate / -fprofile-use=PINMAME_PGO_DIR/default.profdata
#                (merge the .profraw files with llvm-profdata first)
#    MSVC:       /GENPROFILE / /USEPROFILE with PINMAME_PGO_DIR/<target>.pgd,
#                Release only; the instrumented binary needs pgort140.dll and
#                writes <target>!N.pgc when it exits or unloads
#  tests/pinheck/perf/pgo.sh does both sdl3pinmame builds and the training run.
#  The profile is local and never committed.
#
#  Usage, after the target is defined and before cmake/asmjit.cmake and
#  cmake/p2k.cmake (which copy the target's options):
#      include(${CMAKE_SOURCE_DIR}/cmake/pgo.cmake)
#      pinmame_enable_pgo(<target>)
# ---------------------------------------------------------------------------

set(PINMAME_PGO "" CACHE STRING "Profile-guided optimisation: empty, GEN or USE")
set(PINMAME_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-profile" CACHE PATH "Directory of the PGO profile")
if(NOT PINMAME_PGO MATCHES "^(|GEN|USE)$")
   message(FATAL_ERROR "PINMAME_PGO must be empty, GEN or USE")
endif()

function(pinmame_enable_pgo target)
   if(PINMAME_PGO STREQUAL "")
      return()
   endif()
   if(MSVC)
      file(MAKE_DIRECTORY ${PINMAME_PGO_DIR})
      if(PINMAME_PGO STREQUAL "GEN")
         target_link_options(${target} PRIVATE $<$<CONFIG:RELEASE>:/GENPROFILE:PGD=${PINMAME_PGO_DIR}/${target}.pgd>)
      else()
         target_link_options(${target} PRIVATE $<$<CONFIG:RELEASE>:/USEPROFILE:PGD=${PINMAME_PGO_DIR}/${target}.pgd>)
      endif()
   elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      if(PINMAME_PGO STREQUAL "GEN")
         target_compile_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR} -fprofile-update=atomic -fprofile-prefix-path=${CMAKE_BINARY_DIR})
         target_link_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
      else()
         target_compile_options(${target} PRIVATE -fprofile-use=${PINMAME_PGO_DIR} -fprofile-partial-training -fprofile-prefix-path=${CMAKE_BINARY_DIR} -Wno-missing-profile)
      endif()
   else()
      if(PINMAME_PGO STREQUAL "GEN")
         target_compile_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
         target_link_options(${target} PRIVATE -fprofile-generate=${PINMAME_PGO_DIR})
      else()
         target_compile_options(${target} PRIVATE -fprofile-use=${PINMAME_PGO_DIR}/default.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
      endif()
   endif()
endfunction()
