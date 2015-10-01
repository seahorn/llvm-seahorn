set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Analysis
  BitWriter
  CodeGen
  Core
  IPA
  IPO
  IRReader
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  ScalarOpts
  Support
  Target
  TransformUtils
  Vectorize
  )

# Support plugins.
set(LLVM_NO_DEAD_STRIP 1)

add_llvm_executable(seaopt
  AnalysisWrappers.cpp
  BreakpointPrinter.cpp
  GraphPrinters.cpp
  NewPMDriver.cpp
  Passes.cpp
  PassPrinters.cpp
  PrintSCC.cpp
  opt.cpp
  )
target_link_libraries (seaopt SeaLlvmIpo SeaInstCombine SeaInstNondet SeaLoops)

set_target_properties(seaopt PROPERTIES ENABLE_EXPORTS 1)
install(TARGETS seaopt RUNTIME DESTINATION bin)

if (SEAHORN_STATIC_EXE)
  set (CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
  set_target_properties(seaopt PROPERTIES ENABLE_EXPORTS FALSE)
  set_target_properties (seaopt PROPERTIES LINK_SEARCH_START_STATIC ON)
  set_target_properties (seaopt PROPERTIES LINK_SEARCH_END_STATIC ON)
endif()


if(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
  target_link_libraries(seaopt Polly)
  if(POLLY_LINK_LIBS)
    foreach(lib ${POLLY_LINK_LIBS})
      target_link_libraries(seaopt ${lib})
    endforeach(lib)
  endif(POLLY_LINK_LIBS)
endif(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
