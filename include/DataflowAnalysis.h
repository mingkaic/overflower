
#ifndef DATAFLOW_ANALYSIS_H
#define DATAFLOW_ANALYSIS_H
// todo: remove
#include <iostream>

#include <deque>
#include <numeric>
#include <unordered_map>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"

#include "utils.h"


namespace analysis {


class WorkList {
  llvm::DenseSet<llvm::BasicBlock*> inList;
  std::deque<llvm::BasicBlock*> work;

public:
  template<typename IterTy>
  WorkList(IterTy i, IterTy e)
    : inList{},
      work{i, e} {
    inList.insert(i,e);
  }

  bool empty() const { return work.empty(); }

  void
  add(llvm::BasicBlock* bb) {
    if (!inList.count(bb)) {
      work.push_back(bb);
    }
  }

  llvm::BasicBlock *
  take() {
    auto* front = work.front();
    work.pop_front();
    inList.erase(front);
    return front;
  }
};

// The dataflow analysis computes three different granularities of results.
// An AbstractValue represents information in the abstract domain for a single
// LLVM Value. An AbstractState is the abstract representation of all values
// relevent to a particular point of the analysis. A DataflowResult contains
// the abstract states before and after each instruction in a function. The
// incoming state for one instruction is the outgoing state from the previous
// instruction. For the first instruction in a BasicBlock, the incoming state is
// keyed upon the BasicBlock itself.
//
// Note: In all cases, the AbstractValue should have a no argument constructor
// that builds constructs the initial value within the abstract domain.

template <typename AbstractValue>
using AbstractState = llvm::DenseMap<llvm::Value*,AbstractValue>;


template <typename AbstractValue>
using DataflowResult =
  llvm::DenseMap<llvm::Value*, AbstractState<AbstractValue>>;


const unsigned long massivePrime = 32452657; // todo: randomly generate later?


template <typename AbstractValue, typename AbstractInfo>
struct ArgInfo {
  static inline std::vector<AbstractValue> getEmptyKey() {
    return {AbstractInfo::getEmptyKey()};
  }
  static inline std::vector<AbstractValue> getTombstoneKey() {
    return {AbstractInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(const std::vector<AbstractValue>& Args) {
    std::vector<unsigned> setHash;
    for (AbstractValue av : Args) {
      setHash.push_back(AbstractInfo::getHashValue(av));
    }

    unsigned total = 0;
    for (size_t i = 0; i < setHash.size(); i++) {
      total = (total + i * setHash[i]) % massivePrime;
    }
    return total;
  }
  static bool isEqual(const std::vector<AbstractValue>& lhs, const std::vector<AbstractValue>& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(),
    [](const AbstractValue& av1, const AbstractValue& av2) {
      return AbstractInfo::isEqual(av1, av2);
    });
  }
};


template <typename AbstractValue, typename AbstractInfo>
using Arg2Ret = llvm::DenseMap<std::vector<AbstractValue>,AbstractValue,ArgInfo<AbstractValue, AbstractInfo> >;


template <typename AbstractValue, typename AbstractInfo>
using Summary = llvm::DenseMap<llvm::Function*,Arg2Ret<AbstractValue, AbstractInfo> >;


template <typename AbstractValue>
bool
operator==(const AbstractState<AbstractValue>& s1,
           const AbstractState<AbstractValue>& s2) {
  if (s1.size() != s2.size()) {
    return false;
  }
  return std::all_of(s1.begin(), s1.end(),
    [&s2] (auto &kvPair) {
      auto found = s2.find(kvPair.first);
      return found != s2.end() && found->second == kvPair.second;
    });
}


template <typename AbstractValue>
AbstractState<AbstractValue>&
getIncomingState(DataflowResult<AbstractValue>& result, llvm::Instruction& i) {
  auto* bb = i.getParent();
  auto* key = (&bb->front() == &i)
    ? static_cast<llvm::Value*>(bb)
    : static_cast<llvm::Value*>(&*--llvm::BasicBlock::iterator{i});
  return result[key];
}


// NOTE: This class is not intended to be used. It is only intended to
// to document the structure of a Transfer policy object as used by the
// DataflowAnalysis class. For a specific analysis, you should implement
// a class with the same interface.
template <typename AbstractValue>
class Transfer {
public:
  void
  operator()(llvm::Instruction& i, AbstractState<AbstractValue>& s) {
    llvm_unreachable("unimplemented transfer");
  }
};


// This class can be extended with a concrete implementation of the meet
// operator for two elements of the abstract domain. Implementing the
// `meetPair()` method in the subclass will enable it to be used within the
// general meet operator because of the curiously recurring template pattern.
template <typename AbstractValue, typename SubClass>
class Meet {
  SubClass& asSubClass() { return static_cast<SubClass&>(*this); };
public:
  AbstractValue
  operator()(llvm::ArrayRef<AbstractValue> values) {
    return std::accumulate(values.begin(), values.end(),
      AbstractValue(),
      [this] (auto v1, auto v2) {
        return this->asSubClass().meetPair(v1, v2);
      });
  }

  AbstractValue
  meetPair(AbstractValue& v1, AbstractValue& v2) const {
    llvm_unreachable("unimplemented meet");
  }
};


template <typename AbstractValue,
          typename Transfer,
          typename Meet>
class ForwardDataflowAnalysis {
private:
  using State  = AbstractState<AbstractValue>;
  using Result = DataflowResult<AbstractValue>;

  // These property objects determine the behavior of the dataflow analysis.
  // They should by replaced by concrete implementation classes on a per
  // analysis basis.
  Meet meet;
  Transfer transfer;
  std::vector<unsigned> context;

  State
  mergeStateFromPredecessors(llvm::BasicBlock* bb, Result& results) {
    auto mergedState = State{};
    for (auto* p : llvm::predecessors(bb)) {
      auto predecessorFacts = results.find(p->getTerminator());
      if (results.end() == predecessorFacts) {
        continue;
      }

      auto& toMerge = predecessorFacts->second;
      for (auto& valueStatePair : toMerge) {
        // If an incoming Value has an AbstractValue in the already merged
        // state, meet it with the new one. Otherwise, copy the new value over,
        // implicitly meeting with bottom.
        auto found = mergedState.insert(valueStatePair);
        if (!found.second) {
          found.first->second =
            meet({found.first->second, valueStatePair.second});
        }
      }
    }
    return mergedState;
  }

  AbstractValue
  meetOverPHI(const State& state, const llvm::PHINode& phi) {
    auto phiValue = AbstractValue();
    for (auto& value : phi.incoming_values()) {
      auto found = state.find(value.get());
      if (state.end() != found) {
        phiValue = meet({phiValue, found->second});
      }
      else if (llvm::Constant* c = llvm::dyn_cast<llvm::Constant>(value.get())) {
        phiValue = meet({phiValue, AbstractValue(c)});
      }
    }
    return phiValue;
  }

  void
  applyTransfer(llvm::Instruction& i, State& state) {
    // All phis are explicit meet operations
    if (auto* phi = llvm::dyn_cast<llvm::PHINode>(&i)) {
      state[phi] = meetOverPHI(state, *phi);
    }
    else {
      transfer(i, state, context);
    }
  }

public:
  ForwardDataflowAnalysis (std::vector<unsigned> callsites = {}) : context(callsites) {}

  template <typename AbInfo>
  DataflowResult<AbstractValue>
  computeForwardDataflow(Summary<AbstractValue, AbInfo>& summaries, llvm::Function& f, std::vector<AbstractValue>& Args) {
    // First compute the initial outgoing state of all instructions
    Result results;
    for (auto& i : llvm::instructions(f)) {
      results.FindAndConstruct(&i);
    }

    // Associate function arguments with aggregate abstraction
    llvm::Function::arg_iterator arg = f.arg_begin();
    State ogState;
    for (size_t i = 0; i < Args.size() && arg != f.arg_end(); i++) {
      llvm::Value* v = dynamic_cast<llvm::Value*>(&*arg);
      if (v) {
        ogState[v] = Args[i];
      }
      arg++;
    }

    // Add all blocks to the worklist in topological order for efficiency
    llvm::ReversePostOrderTraversal<llvm::Function*> rpot(&f);
    WorkList work(rpot.begin(), rpot.end());

    while (!work.empty()) {
      auto* bb = work.take();

      // Save a copy of the outgoing abstract state to check for changes.
      const auto& oldEntryState = results[bb];
      const auto oldExitState   = results[bb->getTerminator()];

      // Merge the state coming in from all predecessors
      auto state = mergeStateFromPredecessors(bb, results);

      // If we have already processed the block and no changes have been made to
      // the abstract input, we can skip processing the block. Otherwise, save
      // the new entry state and proceed processing this block.
      if (state == oldEntryState && !state.empty()) {
        continue;
      }
      results[bb] = state;
      for (auto oparam : ogState) {
        if (state.end() != state.find(oparam.first)) break;
        state[oparam.first] = oparam.second;
      }

      bool boundChecked = false;

      // Propagate through all instructions in the block
      for (auto& i : *bb) {
        if (auto* call = llvm::dyn_cast<llvm::CallInst>(&i)) {
          llvm::Function* func = call->getCalledFunction();
          if (func->isDeclaration()) {
          	continue;
          }
          unsigned nargs = call->getNumArgOperands();
          std::vector<AbstractValue> argav;
          for (unsigned i = 0; i < nargs; i++) {
            llvm::Value* a = call->getOperand(i);
            auto possibleState = state.find(a);
            if (state.end() != possibleState) {
              argav.push_back(possibleState->second);
            }
            else if (llvm::Constant* c = llvm::dyn_cast<llvm::Constant>(a)) {
              argav.push_back(AbstractValue(c));
            }
            else { // not a constant, nor a stated variable, so it's undefined
              argav.push_back(AbstractValue());
            }
          }
          // push an undefined in if call has no arguments
          if (argav.empty()) {
            argav.push_back(AbstractValue());
          }
          if (summaries[func].end() == summaries[func].find(argav)) {
            summaries[func][argav] = AbstractValue(); // default function return state to undefined in case of recursive calls
            if (context.size() <= 2) { // fixed depth of 2 to bound for efficiency (todo: make main argument?)
              // deeper analyze of func
              optional<unsigned> callsiteno = getLineNumber(i);
              if (callsiteno) { // only proceed if we have context info, otherwise don't proceed
                std::vector<unsigned> concpy = context;
                concpy.push_back(callsiteno.value());
                ForwardDataflowAnalysis<AbstractValue, Transfer, Meet> analysis(concpy);
                analysis.computeForwardDataflow<AbInfo>(summaries, *func, argav);
              }
            }
            else {
              // todo: define summaries[func][argav] as Top to differentiate errors in function
            }
          }
          state[call] = summaries[func][argav];
        }
        else if (auto* ret = llvm::dyn_cast<llvm::ReturnInst>(&i)) {
          llvm::Value* retv = ret->getReturnValue();
          if (llvm::Constant* c = llvm::dyn_cast<llvm::Constant>(retv)) {
            summaries[&f][Args] = AbstractValue(c);
          }
          else {
            auto retav = state.find(retv);
            if (state.end() == retav) {
              summaries[&f][Args] = AbstractValue();
            }
            else {
              summaries[&f][Args] = retav->second;
            }
          }
        }
        // control block analysis based on conditions and mapped bounds
        else if (auto* comp = llvm::dyn_cast<llvm::CmpInst>(&i)) {
          llvm::Value* lhs = comp->getOperand(0);
          llvm::Value* rhs = comp->getOperand(1);

          llvm::Constant* lc = llvm::dyn_cast<llvm::Constant>(lhs);
          llvm::Constant* rc = llvm::dyn_cast<llvm::Constant>(rhs);
          if (lc && rc) continue; // ok...

          auto ldep = state.find(lhs);
          auto rdep = state.find(rhs);

          // todo: deduce lhs or rhs intervals to preserve variable abstraction in successor blocks
          if ((state.end() != ldep && state.end() != rdep) ||
              (nullptr == lc && nullptr == rc)) {

          }
          // define states for true block
          else if (state.end() != ldep && rc) {
            state[ldep->first] = AbstractValue(rc, comp->getPredicate(), &ldep->second);
          }
          else if (state.end() != rdep && lc) {
            state[rdep->first] = AbstractValue(lc, comp->getPredicate(), &rdep->second);
          }
        }
        else if (llvm::BranchInst* br = llvm::dyn_cast<llvm::BranchInst>(&i)) {
          if (br->isConditional() && state.end() == state.find(br->getCondition())) {
            boundChecked = true; // continue to the next block, since we have the condition variable's bounds already set
          }
        }
        else {
          applyTransfer(i, state);
        }
        results[&i] = state;
      }

// todo: remove
//      std::cout << "==============" << bb << "==============\n";
//      for (auto spair: state) {
//        std::cout << spair.first << " ";
//        if (spair.second.range) {
//          std::cout << spair.second.range->first << ":"
//                  << spair.second.range->second << "\n";
//        }
//        else {
//          std::cout << "undefined\n";
//        }
//      }

      // If the abstract state for this block did not change, then we are done
      // with this block. Otherwise, we must update the abstract state and
      // consider changes to successors.
      if (state == oldExitState || boundChecked) {
        continue;
      }

      for (auto* s : llvm::successors(bb)) {
        work.add(s);
      }
    }

    return results;
  }
};


} // end namespace


#endif
