//===- InstCombineWorklist.h - Worklist for InstCombine pass ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SEAHORN_TRANSFORMS_INSTCOMBINE_INSTCOMBINEWORKLIST_H
#define LLVM_SEAHORN_TRANSFORMS_INSTCOMBINE_INSTCOMBINEWORKLIST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "sea-instcombine"

namespace llvm_seahorn {
  
/// InstCombineWorklist - This is the worklist management logic for
/// InstCombine.
class InstCombineWorklist {
  llvm::SmallVector<llvm::Instruction*, 256> Worklist;
  llvm::DenseMap<llvm::Instruction*, unsigned> WorklistMap;

public:
  InstCombineWorklist() = default;

  InstCombineWorklist(InstCombineWorklist &&) = default;
  InstCombineWorklist &operator=(InstCombineWorklist &&) = default;

  bool isEmpty() const { return Worklist.empty(); }

  /// Add - Add the specified instruction to the worklist if it isn't already
  /// in it.
  void Add(llvm::Instruction *I) {
    if (WorklistMap.insert(std::make_pair(I, Worklist.size())).second) {
      DEBUG(llvm::dbgs() << "IC: ADD: " << *I << '\n');
      Worklist.push_back(I);
    }
  }

  void AddValue(llvm::Value *V) {
    if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V))
      Add(I);
  }

  /// AddInitialGroup - Add the specified batch of stuff in reverse order.
  /// which should only be done when the worklist is empty and when the group
  /// has no duplicates.
  void AddInitialGroup(llvm::ArrayRef<llvm::Instruction *> List) {
    assert(Worklist.empty() && "Worklist must be empty to add initial group");
    Worklist.reserve(List.size()+16);
    WorklistMap.reserve(List.size());
    DEBUG(llvm::dbgs() << "IC: ADDING: " << List.size() << " instrs to worklist\n");
    unsigned Idx = 0;
    for (llvm::Instruction *I : reverse(List)) {
      WorklistMap.insert(std::make_pair(I, Idx++));
      Worklist.push_back(I);
    }
  }

  // Remove - remove I from the worklist if it exists.
  void Remove(llvm::Instruction *I) {
    llvm::DenseMap<llvm::Instruction*, unsigned>::iterator It = WorklistMap.find(I);
    if (It == WorklistMap.end()) return; // Not in worklist.

    // Don't bother moving everything down, just null out the slot.
    Worklist[It->second] = nullptr;

    WorklistMap.erase(It);
  }

  llvm::Instruction *RemoveOne() {
    llvm::Instruction *I = Worklist.pop_back_val();
    WorklistMap.erase(I);
    return I;
  }

  /// AddUsersToWorkList - When an instruction is simplified, add all users of
  /// the instruction to the work lists because they might get more simplified
  /// now.
  ///
  void AddUsersToWorkList(llvm::Instruction &I) {
    for (llvm::User *U : I.users())
      Add(llvm::cast<llvm::Instruction>(U));
  }


  /// Zap - check that the worklist is empty and nuke the backing store for
  /// the map if it is large.
  void Zap() {
    assert(WorklistMap.empty() && "Worklist empty, but map not?");

    // Do an explicit clear, this shrinks the map if needed.
    WorklistMap.clear();
  }
};

} // end namespace llvm_seahorn

#undef DEBUG_TYPE

#endif
