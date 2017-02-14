//
// Created by Mingkai Chen on 2017-02-12.
//

#include "utils.h"

#ifdef OVERFLOWER_UTILS_H


llvm::Constant*
toConstant (int64_t c, llvm::Type* type) {
	return llvm::ConstantInt::getSigned(type, c);
}


optional<int64_t>
toInt (llvm::Constant* c) {
	optional<int64_t> i;
	if (auto* constint = llvm::dyn_cast<llvm::ConstantInt>(c)) {
		i = constint->getSExtValue();
	}
	return i;
}


optional<unsigned>
getLineNumber(llvm::Instruction& inst) {
	optional<unsigned> lino;
	if (const llvm::DILocation* debugLoc = inst.getDebugLoc()) {
		lino = debugLoc->getLine();
	}
	return lino;
}


std::vector<unsigned>
getByteWidth(llvm::Type* ty, unsigned& total) {
	if (llvm::CompositeType* cty = llvm::dyn_cast<llvm::CompositeType>(ty)) {
		llvm::Type *baseTy = cty->getTypeAtIndex((unsigned) 0);
		unsigned basebyte;
		getByteWidth(baseTy, basebyte);
		// aligned accessors
		if (llvm::ArrayType *aty = llvm::dyn_cast<llvm::ArrayType>(ty)) {
			total = aty->getNumElements() * basebyte;
			return std::vector<unsigned>(total / basebyte, basebyte);
		}
		else if (llvm::VectorType *vty = llvm::dyn_cast<llvm::VectorType>(ty)) {
			total = vty->getNumElements() * basebyte;
			return std::vector<unsigned>(total / basebyte, basebyte);
		}
		// possibly non-aligned accessors
		else if (llvm::PointerType *pty = llvm::dyn_cast<llvm::PointerType>(ty)) {
			total = pty->getAddressSpace();
			// treat everything as continugous single bytes
			return std::vector<unsigned>(total, 1);
		}
		else if (llvm::StructType *sty = llvm::dyn_cast<llvm::StructType>(ty)) {
			total = 0;
			unsigned totale = sty->getNumElements();
			std::vector<unsigned> bytes;
			for (unsigned e = 0; e < totale; e++) {
				unsigned b;
				getByteWidth(sty->getElementType(e), b);
				bytes.push_back(b);
				total += b;
			}
			return bytes;
		}
	}
	else if (llvm::IntegerType* ity = llvm::dyn_cast<llvm::IntegerType>(ty)) {
		total = ity->getBitWidth() / 8;
		return std::vector<unsigned>(1, total);
	}
	// function type?
	total = 0;
	return std::vector<unsigned>();
}


#endif