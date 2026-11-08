# SPDX-FileCopyrightText: 2026 The llvm-seahorn Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_llvm_library(SeaLlvmIpo DISABLE_LLVM_LINK_LLVM_DYLIB
  Annotation2Metadata.cpp
  LoopExtractor.cpp
  )
