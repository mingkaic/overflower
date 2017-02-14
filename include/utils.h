//
// Created by Mingkai Chen on 2017-02-12.
//

#ifndef OVERFLOWER_UTILS_H
#define OVERFLOWER_UTILS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Constants.h"

#include <experimental/optional>


using namespace std::experimental;


llvm::Constant*
toConstant (int64_t c, llvm::Type* type);


optional<int64_t>
toInt (llvm::Constant* c);


optional<unsigned>
getLineNumber(llvm::Instruction& inst);


std::vector<unsigned>
getByteWidth(llvm::Type* ty, unsigned& total);


#endif //OVERFLOWER_UTILS_H
