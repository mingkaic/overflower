//
// Created by Mingkai Chen on 2017-02-09.
//

#include "llvm/IR/CallSite.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/APSInt.h"
#include "llvm/Analysis/ConstantFolding.h"

#include "DataflowAnalysis.h"

#include <utility>
#include <fstream>
#include <iostream>
#include <functional>

#ifndef OVERFLOWER_OVERFLOWER_H
#define OVERFLOWER_OVERFLOWER_H


using namespace llvm;

// define infinity for indices as a rather small value to account for byte multiplication
const int64_t INF 		= 0x0000ffffffffffff;
const int64_t NEGINF 	= -INF;


// c++17 upgrade: can make variant to better represent trinary states
using BOUND = optional<std::pair<int64_t,int64_t> >;


struct BoundValue {
	BOUND range; // undefined by default
	// todo: use range_entropy for widening, and updated during meet and transfer
	float range_entropy = 0.0;
	// retain type in order to better approximate error bounds
	Type* boundType = nullptr;

	BoundValue() {}

	explicit BoundValue(llvm::Constant* value,
		llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ,
		const BoundValue* prevState = nullptr);

	explicit BoundValue(BOUND range, Type* boundType);

	explicit BoundValue(const BoundValue& v,
		std::function<optional<int64_t>(int64_t,Type*)> eval);

	explicit BoundValue(const BoundValue& v1, const BoundValue& v2,
		std::function<optional<int64_t>(int64_t,int64_t,Type*)> eval);

	BoundValue
	operator | (const BoundValue& other) const;

	bool
	operator == (const BoundValue& other) const;

	bool
	hasRange() const;

	bool
	isInf() const {
		return range && NEGINF == range->first && INF == range->second;
	}
};


struct BoundInfo {
	static inline BoundValue getEmptyKey() {
		return BoundValue(BOUND({NEGINF, NEGINF}), nullptr);
	}
	static inline BoundValue getTombstoneKey() {
		return BoundValue(BOUND({NEGINF, NEGINF}), nullptr);
	}
	static unsigned getHashValue(const BoundValue& Val) {
		if (Val.range) {
			int64_t lower = Val.range->first;
			int64_t upper = Val.range->second;
			// hash by cantor pairing depending on sign
			// with low probability of overflow...
			unsigned A = (unsigned)(lower >= 0 ? 2 * (long)lower : -2 * (long)lower - 1);
			unsigned B = (unsigned)(upper >= 0 ? 2 * (long)upper : -2 * (long)upper - 1);
			long C = (long)((A >= B ? A * A + A + B : A + B * B) / 2);
			return ((lower < 0 && upper < 0) || (lower >= 0 && upper >= 0) ? C : -C - 1)+1;
		}
		return 0;
	}
	static bool isEqual(const BoundValue& lhs, const BoundValue& rhs) {
		if (lhs.hasRange() == rhs.hasRange()) {
			if (lhs.hasRange()) {
				// todo: approximate bounds by defining an error threshold scaling with value bit size
				return lhs.range->first == rhs.range->first && lhs.range->second == rhs.range->second;
			}
			else {
				return true;
			}
		}
		return false;
	}
};


using BoundState  = analysis::AbstractState<BoundValue>;
using BoundResult = analysis::DataflowResult<BoundValue>;
using BoundSummary = analysis::Summary<BoundValue, BoundInfo>;


class BoundMeet : public analysis::Meet<BoundValue, BoundMeet> {
public:
	BoundValue
	meetPair(BoundValue& s1, BoundValue& s2) const;
};


class BoundTransfer {
	BoundValue
	getBoundValueFor(llvm::Value* v, BoundState& state) const;

	BoundValue
	evaluateBinaryOperator(llvm::BinaryOperator& binOp,
						   BoundState& state) const;

	BoundValue
	evaluateCast(llvm::CastInst& castOp, BoundState& state) const;

public:
	void
	operator()(llvm::Instruction& i, BoundState& state, std::vector<unsigned>& context);
};


void
printErrors(std::ostream& out);


void
clearReports();


#endif //OVERFLOWER_OVERFLOWER_H
