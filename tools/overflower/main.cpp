
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Constants.h"
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

#include <bitset>
#include <memory>
#include <string>

#include "overflower.h"


using namespace llvm;
using std::string;
using std::unique_ptr;


static cl::OptionCategory overflowerCategory{"overflower options"};

static cl::opt<string> inPath{cl::Positional,
                              cl::desc{"<Module to analyze>"},
                              cl::value_desc{"bitcode filename"},
                              cl::init(""),
                              cl::Required,
                              cl::cat{overflowerCategory}};


static auto
computeBounds(llvm::Function& f, BoundSummary& summaries) {
	analysis::ForwardDataflowAnalysis<BoundValue,
			BoundTransfer,
			BoundMeet> analysis;
    std::vector<BoundValue> Args = {BoundValue()};
	return analysis.computeForwardDataflow(summaries, f, Args);
}

int
main(int argc, char** argv) {
  std::string fname;
  if (2 < argc) {
    fname = argv[2];
  }

  // This boilerplate provides convenient stack traces and clean LLVM exit
  // handling. It also initializes the built in support for convenient
  // command line option handling.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj shutdown;
  cl::HideUnrelatedOptions(overflowerCategory);
  cl::ParseCommandLineOptions(argc, argv);

  // Construct an IR file from the filename passed on the command line.
  SMDiagnostic err;
  LLVMContext context;
  unique_ptr<Module> module = parseIRFile(inPath.getValue(), err, context);

  if (!module.get()) {
    errs() << "Error reading bitcode file: " << inPath << "\n";
    err.print(argv[0], errs());
    return -1;
  }

  BoundSummary summaries;

  for (auto& f : *module) {
    if (f.isDeclaration()) {
      continue;
    }
    auto results = computeBounds(f, summaries);
  }

  std::ofstream fs(fname);
  if (fs.is_open()) {
    printErrors(fs);
    fs.close();
  }
  else {
    printErrors(std::cout);
  }

  clearReports();

  return 0;
}
