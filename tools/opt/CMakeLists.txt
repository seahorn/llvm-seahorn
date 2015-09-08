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
target_link_libraries (seaopt SeaLlvmIpo SeaInstCombine SeaInstNondet)

set_target_properties(seaopt PROPERTIES ENABLE_EXPORTS 1)
install(TARGETS seaopt RUNTIME DESTINATION bin)


if(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
  target_link_libraries(seaopt Polly)
  if(POLLY_LINK_LIBS)
    foreach(lib ${POLLY_LINK_LIBS})
      target_link_libraries(seaopt ${lib})
    endforeach(lib)
  endif(POLLY_LINK_LIBS)
endif(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
