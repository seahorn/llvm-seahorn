# Standalone driver that exercises the SeaHorn loop-extract pass the way seapp
# does (via the createSeaLoopExtractorPass() library API, not through seaopt).
set(LLVM_LINK_COMPONENTS
  Analysis
  BitWriter
  Core
  IRReader
  Passes
  ScalarOpts
  Support
  TransformUtils
  ipo
  )

add_llvm_executable(sea_loop_extract_driver
  sea_loop_extract_driver.cpp

  DISABLE_LLVM_LINK_LLVM_DYLIB
  DEPENDS
  intrinsics_gen
  )

# Our libraries first so they satisfy the Sea* symbols before stock LLVM.
target_link_libraries(sea_loop_extract_driver
  PRIVATE SeaLlvmIpo SeaInstCombine SeaLoops)
