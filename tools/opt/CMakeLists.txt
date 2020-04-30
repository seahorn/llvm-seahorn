set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AggressiveInstCombine
  Analysis
  BitWriter
  CodeGen
  Core
  Coroutines
  IPO
  IRReader
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  Remarks
  ScalarOpts
  Support
  Target
  TransformUtils
  Vectorize
  Passes
  )

add_llvm_executable(seaopt
  AnalysisWrappers.cpp
  CallGraphPrinterWrapper.cc
  BreakpointPrinter.cpp
  GraphPrinters.cpp
  NewPMDriver.cpp
  PassPrinters.cpp
  PrintSCC.cpp
  opt.cpp

  ENABLE_PLUGINS

  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS
  )

export_executable_symbols(seaopt)

target_link_libraries(seaopt PRIVATE SeaLlvmIpo SeaInstCombine SeaLoops)

install(TARGETS seaopt RUNTIME DESTINATION bin)

if(LLVM_BUILD_EXAMPLES)
    target_link_libraries(seaopt PRIVATE ExampleIRTransforms)
endif(LLVM_BUILD_EXAMPLES)
