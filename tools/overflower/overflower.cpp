//
// Created by Mingkai Chen on 2017-02-09.
//

#include "overflower.h"
#include <random>

#ifdef OVERFLOWER_OVERFLOWER_H

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<float> rando(0, 1);

void widen (BoundValue& bv) {
	assert(false == isnan(bv.range_entropy));

	if (!bv.isInf()) {
		unsigned interval = bv.range->second - bv.range->first+1;
		unsigned milestone = interval / 256;
		if ((1 - bv.range_entropy) * interval > INF / 4) { // set fairly high threshold for moving to top
			// bound the real interval
			bv.range = BOUND({NEGINF, INF});
		}
		// we want to encourage range approximation, for faster convergence
		// set ad hoc threshold of 0.5 entropy
		else if (bv.range_entropy < 0.5) {
			if (milestone >= 1) {
				double growth = std::log(interval/2);
				for (size_t i = 0; i < milestone; i++) {
					bv.range->second += growth;
					bv.range->first -= growth;
				}
				bv.range_entropy *= 1.0 + (milestone * growth / interval);
			}
		}
	}
}


unsigned getOverlap(BOUND interv1, BOUND interv2) {
	assert(interv1 && interv2);
	int64_t top = std::min(interv1->second, interv2->second);
	int64_t bot = std::max(interv1->first, interv2->first);
	if (top < bot) return 0;
	return top - bot;
}


std::pair<float, BOUND> BoundValue::predicateBound(int64_t value,
	llvm::CmpInst::Predicate pred,
	const BoundValue* prevState) const {
	int64_t lower = NEGINF;
	int64_t upper = INF;
	float potential_entropy = rando(gen);
	BOUND fbound;

	if (prevState != nullptr && prevState->range) {
		lower = prevState->range->first;
		upper = prevState->range->second;
		potential_entropy += prevState->range_entropy;
		potential_entropy /= 2;
	}

	switch (pred) {
		case llvm::CmpInst::FCMP_OEQ:
		case llvm::CmpInst::FCMP_UEQ:
		case llvm::CmpInst::ICMP_EQ:
			fbound = BOUND({value, value});
			potential_entropy = range_entropy;
			break;

			// for ranges with undefined upper or lower bound, assign noise to simulate
			// random default value of variable
		case llvm::CmpInst::FCMP_OLT:
		case llvm::CmpInst::FCMP_ULT:
		case llvm::CmpInst::ICMP_ULT:
		case llvm::CmpInst::ICMP_SLT:
			fbound = BOUND({lower, value-1});
			break;

		case llvm::CmpInst::FCMP_OLE:
		case llvm::CmpInst::FCMP_ULE:
		case llvm::CmpInst::ICMP_ULE:
		case llvm::CmpInst::ICMP_SLE:
			fbound = BOUND({lower, value});
			break;

		case llvm::CmpInst::FCMP_OGT:
		case llvm::CmpInst::FCMP_UGT:
		case llvm::CmpInst::ICMP_UGT:
		case llvm::CmpInst::ICMP_SGT:
			fbound = BOUND({value+1, upper});
			break;

		case llvm::CmpInst::FCMP_OGE:
		case llvm::CmpInst::FCMP_UGE:
		case llvm::CmpInst::ICMP_UGE:
		case llvm::CmpInst::ICMP_SGE:
			fbound = BOUND({value, upper});
			break;

		default:
			break;
	}
	return {potential_entropy, fbound};
}


BoundValue::BoundValue(llvm::Constant* value,
	llvm::CmpInst::Predicate pred,
	const BoundValue* prevState)
	: boundType(value->getType())
{
	if (auto* constint = dyn_cast<ConstantInt>(value)) {
		int64_t val = constint->getSExtValue();
		std::pair<float, BOUND> p = predicateBound(val, pred, prevState);
		range_entropy = p.first;
		range = p.second;
		widen(*this);
	}
}

BoundValue::BoundValue(const BoundValue& other,
	llvm::CmpInst::Predicate pred,
	const BoundValue* prevState)
	: boundType(other.boundType)
{
	if (other.range) {
		std::pair<float, BOUND> p = predicateBound(other.range->first, pred, prevState);
		std::pair<float, BOUND> p2 = predicateBound(other.range->second, pred, prevState);

		range_entropy = (p.first + p2.first) / 2;
		range = BOUND({
			std::min(p.second->first, p2.second->first),
			std::max(p.second->second, p2.second->second)
		});
		widen(*this);
	}
}

BoundValue::BoundValue(BOUND range, Type* boundType) : range(range), boundType(boundType) {
	widen(*this);
}

BoundValue::BoundValue(const BoundValue& v,
	std::function<optional<int64_t>(int64_t,Type*)> eval)
{
	if (v.isInf()) {
		range = v.range;
		range_entropy = v.range_entropy;
		return;
	}
	boundType = v.boundType;
	int64_t min1 = v.range->first;
	int64_t max1 = v.range->second;
	std::vector<int64_t> candidates = {min1, max1};
	optional<int64_t> cand = eval(min1, boundType);
	if (cand) {
		candidates.push_back(cand.value());
		candidates.push_back(eval(max1, boundType).value());
		range = BOUND({
			*(std::min_element(candidates.begin(), candidates.end())),
			*(std::max_element(candidates.begin(), candidates.end()))
		});
		range_entropy = v.range_entropy;
	}
	widen(*this);
}

BoundValue::BoundValue(const BoundValue& v1, const BoundValue& v2,
	std::function<optional<int64_t>(int64_t,int64_t,Type*)> eval)
{
	if (v1.isInf()) {
		range = v1.range;
		range_entropy = v1.range_entropy;
		return;
	}
	if (v2.isInf()) {
		range = v2.range;
		range_entropy = v2.range_entropy;
		return;
	}
	boundType = v1.boundType;
	int64_t min1 = v1.range->first;
	int64_t max1 = v1.range->second;
	int64_t min2 = v2.range->first;
	int64_t max2 = v2.range->second;
	std::vector<int64_t> candidates;
	optional<int64_t> cand = eval(min1, min2, boundType);
	if (cand) {
		candidates.push_back(cand.value());
		candidates.push_back(eval(max1, max2, boundType).value());
		candidates.push_back(eval(min1, max2, boundType).value());
		candidates.push_back(eval(max1, min2, boundType).value());
		// todo: account for undefined behavior like divide by zero
		if (min1 < 0 && max1 > 0) {
			candidates.push_back(eval(0, min2, boundType).value());
			candidates.push_back(eval(0, max2, boundType).value());
		}
		if (min2 < 0 && max2 > 0) {
			candidates.push_back(eval(0, min1, boundType).value());
			candidates.push_back(eval(0, max1, boundType).value());
		}
		range = BOUND({
			*(std::min_element(candidates.begin(), candidates.end())),
			*(std::max_element(candidates.begin(), candidates.end()))
		});
		// mean entropy value on transfer function
		range_entropy = (v1.range_entropy + v2.range_entropy) / 2;
	}
	widen(*this);
}

BoundValue
BoundValue::operator | (const BoundValue& other) const {
	if (*this == other) {
		return *this;
	}

	else if (hasRange() && other.hasRange()) {
		BoundValue met(BOUND({
			std::min(range->first, other.range->first),
			std::max(range->second, other.range->second)
		}), boundType);
		// determine range_entropy by overlap
		unsigned overlap = getOverlap(range, other.range);
		float tpercent = overlap / (range->second - range->first + 1);
		float opercent = overlap / (other.range->second - other.range->first + 1);
		met.range_entropy = (1.0-tpercent) * range_entropy + (1.0-opercent) * other.range_entropy +
							(tpercent*range_entropy + opercent*other.range_entropy) / 2.0;
		widen(met);
		return met;
	}
	else if (hasRange()) {
		return *this;
	}
	return other;
}


bool
BoundValue::operator == (const BoundValue& other) const {
	return BoundInfo::isEqual(*this, other);
}

bool
BoundValue::hasRange() const {
	return bool(range);
}


BoundValue
BoundMeet::meetPair(BoundValue& s1, BoundValue& s2) const {
	return s1 | s2;
}


BoundValue
BoundTransfer::getBoundValueFor(llvm::Value* v, BoundState& state) const {
	if (auto* constant = llvm::dyn_cast<llvm::Constant>(v)) {
		return BoundValue{constant};
	}
	return state[v];
}


BoundValue
BoundTransfer::evaluateBinaryOperator(llvm::BinaryOperator& binOp,
					   BoundState& state) const {
	auto* op1   = binOp.getOperand(0);
	auto* op2   = binOp.getOperand(1);
	auto value1 = getBoundValueFor(op1, state);
	auto value2 = getBoundValueFor(op2, state);

	if (value1.hasRange() && value2.hasRange()) {
		auto& layout = binOp.getModule()->getDataLayout();
		return BoundValue{value1, value2,
		[&binOp, &layout](int64_t v1, int64_t v2, Type* type) -> optional<int64_t> {
			Constant* c1 = toConstant(v1, type);
			Constant* c2 = toConstant(v2, type);
			Constant* ans = ConstantFoldBinaryOpOperands(binOp.getOpcode(), c1, c2, layout);
			return toInt(ans);
		}};
	} else {
		// we're evaluating undefined variables... wat?
		return BoundValue();
	}
}


BoundValue
BoundTransfer::evaluateCast(llvm::CastInst& castOp, BoundState& state) const {
	auto* op   = castOp.getOperand(0);
	auto value = getBoundValueFor(op, state);

	if (value.hasRange()) {
		auto& layout = castOp.getModule()->getDataLayout();
		return BoundValue{value,
		[&castOp, &layout](int64_t v, Type* type) -> optional<int64_t> {
			Constant* c = toConstant(v, type);
			Constant* ans = ConstantFoldCastOperand(castOp.getOpcode(), c,
							castOp.getDestTy(), layout);
			return toInt(ans);
		}};
	} else {
		// we're casting an undefined variable
		return BoundValue();
	}
}


struct ErrReport {
	llvm::Function* f;
	std::vector<unsigned> context;
	size_t lineno;
	size_t buffersize;
	BOUND access;
};


static llvm::DenseSet<ErrReport*> errorLog;
// encode context for call depth of 2 todo: generalize
static std::unordered_map<unsigned, llvm::DenseMap<llvm::Value*, ErrReport*> > potentialError;


static BOUND
checkError(Value* idx, signed limit, BoundState& state) {
	BOUND b;
	if (nullptr == idx) {
		return b;
	}
	auto inst = state.find(idx);
	if (Constant* c = dyn_cast<llvm::Constant>(idx)) {
		optional<int64_t> i = toInt(c);
		assert(i);
		int64_t iidx = i.value();
		return iidx < limit && iidx >= 0 ? b : BOUND({iidx, iidx});
	}
	else if (state.end() != inst){
		b = inst->second.range;
		if (b) {
			return b->second < limit && b->first >= 0 ? BOUND() : b;
		}
		else {
			return BOUND({NEGINF, INF});
		}
	}
	else {
		return b;
	}
}


static unsigned
contextEncode(std::vector<unsigned>& context) {
	unsigned total = 0;
	for (size_t i = 0; i < context.size(); i++) {
		total += (i+1) * context[i] % massivePrime;
	}
	return total;
}


void
BoundTransfer::operator()(llvm::Instruction& i, BoundState& state, std::vector<unsigned>& context) {
	// error check instruction
	// if error, then state is instantly undefined
	if (GetElementPtrInst* gep = llvm::dyn_cast<GetElementPtrInst>(&i)) {
		unsigned limit = 0;
		Value* idx = gep->getOperand(2);
		std::vector<unsigned> byteWidth = getByteWidth(gep->getSourceElementType(), limit);

		if (BOUND b = checkError(idx, byteWidth.size(), state)) {
			state[&i] = BoundValue();
			optional<unsigned> lineno = getLineNumber(i);
			b->first *= byteWidth.front();
			b->second *= byteWidth.back();
			if (lineno) {
				// cache this as potential error, wrt to i, then log if and only if there is a store/read on instruction i
				potentialError[contextEncode(context)].insert({gep, new ErrReport{ i.getFunction(), context, lineno.value(), limit, b }});
			}
		}
	}
	else if (LoadInst* getter = llvm::dyn_cast<llvm::LoadInst>(&i)) {
		auto gpair = potentialError[contextEncode(context)].find(getter->getPointerOperand());
		if (potentialError[contextEncode(context)].end() != gpair) {
			errorLog.insert(gpair->second);
		}
	}
	else if (StoreInst* setter = llvm::dyn_cast<llvm::StoreInst>(&i)) {
		auto spair = potentialError[contextEncode(context)].find(setter->getPointerOperand());
		if (potentialError[contextEncode(context)].end() != spair) {
			errorLog.insert(spair->second);
		}
	}
	// actual transfer functions
	else if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(&i)) {
		state[binOp] = evaluateBinaryOperator(*binOp, state);
	}
	else if (auto* castOp = llvm::dyn_cast<llvm::CastInst>(&i)) {
		state[castOp] = evaluateCast(*castOp, state);
	}
	else if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&i)) {
		Value* rhs = alloca->getOperand(0);
		if (Constant* crhs = llvm::dyn_cast<llvm::Constant>(rhs)) {
			state[alloca] = BoundValue(crhs);
		}
		else {
			state[alloca] = state[rhs];
		}
	}
	else { // others
		state.insert({&i, BoundValue()});
	}
}


void
printErrors(std::ostream& out) {
	for (ErrReport* report : errorLog) {
		if (!report->context.empty()) {
			out << report->context.front();
			for (auto it = ++report->context.begin(); it != report->context.end(); it++) {
				out << ":" << *it;
			}
		}
		out << ", " << report->f->getName().data() << ", " << report->lineno << ", " << report->buffersize << ", ";
		int64_t bot = report->access->first;
		int64_t top = report->access->second;
		if (bot <= NEGINF) {
			out << "-inf:";
		}
		else {
			out << bot << ":";
		}
		if (top >= INF) {
			out << "inf\n";
		} else {
			out << top << "\n";
		}
	}
}


void
clearReports() {
	for (ErrReport* report : errorLog) {
		delete report;
	}
	errorLog.clear();
}


#endif