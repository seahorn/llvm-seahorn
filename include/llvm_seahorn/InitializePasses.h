//===- llvm/InitializePasses.h - Initialize All Passes ----------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for the pass initialization routines
// for the entire LLVM project.
//
//===----------------------------------------------------------------------===//

#ifndef SEA_LLVM_INITIALIZEPASSES_H
#define SEA_LLVM_INITIALIZEPASSES_H
#include "llvm/InitializePasses.h"

namespace llvm {
void initializeSeaIndVarSimplifyLegacyPassPass(PassRegistry &);
void initializeSeaInstructionCombiningPassPass(PassRegistry &);
void initializeSeaLoopRotateLegacyPassPass(PassRegistry &);
void initializeSeaLoopUnrollPass(PassRegistry &);
} // end namespace llvm

#endif // SEA_LLVM_INITIALIZEPASSES_H
