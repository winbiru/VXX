#include "vpp/native/aot_runtime.h"
#include "common/vm_utils.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace vietvm::native::aot;

extern "C" int vpp_aot_assign_slot(int, int) noexcept;
extern "C" int vpp_aot_store_integer(int, int, int) noexcept;
extern "C" int vpp_aot_return_slot(int, int) noexcept;
extern "C" int vpp_aot_return_integer(int, int) noexcept;

namespace {
void require(bool condition, const char *description) {
    if (!condition) throw std::runtime_error(description);
}

// Reference implementation using the existing dynamic Frame operations.
// In particular OP_CONG_MOT leaves every incremented counter on the stack.
void reference(Frame &f, int bound, int operand, int operation,
               bool counted, int threshold) {
    for (;;) {
        f.push(f.load(0));
        f.push(bound);
        f.binary(OP_NHO_HON);
        if (!vietvm::runtime::stackValueTruthy(f.pop())) break;
        f.push(f.load(1));
        f.push(operand);
        f.binary(operation);
        f.push(1);
        f.assign();
        if (counted) {
            f.push(f.load(1));
            f.push(threshold);
            f.binary(OP_LON_HON_HOAC_BANG);
            if (vietvm::runtime::stackValueTruthy(f.pop())) {
                f.push(f.load(1));
                f.push(threshold);
                f.binary(OP_TRU);
                f.push(1);
                f.assign();
            }
        }
        f.incrementSlot(0, 1);
    }
}

void compare(int initial, int bound, Value initialValue, int operand,
             int operation, bool counted = false, int threshold = 0) {
    Program program;
    Frame optimized(program), slow(program);
    for (Frame *f : {&optimized, &slow}) {
        f->store(0, initial);
        f->store(1, initialValue);
        f->push(777); // Make stack history observable across the fast region.
    }
    const bool accepted = counted
        ? optimized.runCountedScalarLoop(0, bound, 1, operand, threshold)
        : optimized.runSimpleScalarLoop(0, bound, 1, operand, operation);
    require(accepted, "typed scalar loop unexpectedly deoptimized");
    reference(slow, bound, operand, operation, counted, threshold);
    require(optimized.load(0) == slow.load(0), "counter differs");
    const auto &a = optimized.valueAt(1), &b = slow.valueAt(1);
    require(a.index() == b.index(), "numeric type differs");
    if (const auto *x = std::get_if<int>(&a))
        require(*x == std::get<int>(b), "integer result differs");
    else if (const auto *x = std::get_if<double>(&a)) {
        const auto y = std::get<double>(b);
        require((*x == y) || (std::isnan(*x) && std::isnan(y)),
                "floating result differs");
    } else require(false, "unexpected numeric value");
    require(optimized.stack.size() == slow.stack.size(), "stack depth differs");
    for (std::size_t i = 0; i < slow.stack.size(); ++i)
        require(std::get<int>(optimized.stack[i]) == std::get<int>(slow.stack[i]),
                "increment result/order differs");
}

void compareNumericAdd(int initial, int bound, int leftSlot,
                       Value right, Value destination,
                       bool shareResultWithRight = false) {
    Program program;
    Frame optimized(program), slow(program);
    for(Frame *f:{&optimized,&slow}) {
        f->store(0,initial);
        if(shareResultWithRight) {
            auto shared=std::make_shared<vietvm::runtime::RuntimeCell>();
            f->locals[2]=shared;
            f->locals[3]=shared;
        }
        f->store(2,right);
        if(!shareResultWithRight)f->store(3,destination);
        f->push(777);
    }
    require(optimized.runNumericAddLoop(0,bound,leftSlot,2,3),
            "typed numeric addition loop unexpectedly deoptimized");
    for(int i=initial;i<bound;++i) {
        slow.push(slow.load(leftSlot));
        slow.push(slow.load(2));
        slow.binary(OP_CONG);
        slow.store(3,slow.pop());
        slow.incrementSlot(0,1);
    }
    require(optimized.load(0)==slow.load(0),"numeric loop counter differs");
    const Value &actual=optimized.valueAt(3), &expected=slow.valueAt(3);
    if(actual.index()!=expected.index())
        throw std::runtime_error("numeric loop output type differs: initial="+
            std::to_string(initial)+" bound="+std::to_string(bound)+
            " left="+std::to_string(leftSlot)+" right_type="+
            std::to_string(right.index())+" destination_type="+
            std::to_string(destination.index())+" shared="+
            std::to_string(shareResultWithRight));
    if(const auto *a=std::get_if<int>(&actual))
        require(*a==std::get<int>(expected),"numeric loop i32 result differs");
    else if(const auto *a=std::get_if<double>(&actual)) {
        const double b=std::get<double>(expected);
        require((*a==b)||(std::isnan(*a)&&std::isnan(b)),
                "numeric loop f64 result differs");
    } else require(false,"numeric loop produced a non-number");
    require(optimized.stack==slow.stack,"numeric loop increment history differs");
}

void verifyNumericAddDeopt() {
    Program program;
    Frame f(program);
    f.store(0,0);
    f.store(2,0.25);
    f.store(3,7); // Promoted, aliased i32 source changes type.
    f.push(777);
    require(!f.runNumericAddLoop(0,10,3,2,3),
            "promoted aliased i32 source must deopt");
    require(f.load(3)==Value(7)&&f.stack==std::vector<Value>{777},
            "numeric deopt modified a live binding or operand stack");
    require(!f.runNumericAddLoop(0,10,0,2,0),
            "induction variable assigned by body must deopt");
    require(!f.runNumericAddLoop(0,10,4,2,3),
            "missing numeric source must deopt");
    auto captured=std::make_shared<vietvm::runtime::RuntimeCell>();
    captured->value=0;
    f.locals[0]=captured;
    f.locals[3]=captured;
    require(!f.runNumericAddLoop(0,10,0,2,3),
            "shared induction/destination cell must deopt");
}

void deopt(int counter, int bound, Value value, int operation) {
    Program program;
    Frame f(program);
    f.store(0, counter);
    f.store(1, value);
    f.push(83);
    require(!f.runSimpleScalarLoop(0, bound, 1, 2, operation),
            "unsupported scalar loop must deopt");
    require(f.load(0) == Value(counter) && f.load(1).index() == value.index(),
            "deopt modified a live slot");
    require(f.stack.size() == 1 && std::get<int>(f.stack[0]) == 83,
            "deopt modified operand stack");
}

void aliasDeopt(bool counted) {
    Program program;
    Frame f(program);
    auto shared = std::make_shared<vietvm::runtime::RuntimeCell>();
    shared->value = 0;
    f.locals[0] = shared;
    f.locals[1] = shared;
    f.push(83);
    const bool accepted = counted
        ? f.runCountedScalarLoop(0, 4, 1, 1, 9)
        : f.runSimpleScalarLoop(0, 4, 1, 1, OP_CONG);
    require(!accepted, "aliased induction/value bindings must deopt");
    require(shared->value == Value(0), "aliased deopt changed the shared cell");
    require(f.stack.size() == 1 && std::get<int>(f.stack[0]) == 83,
            "aliased deopt changed the operand stack");
}

void absentValueRemainsAbsent(bool counted) {
    Program program;
    Frame f(program);
    f.store(0, 4);
    require(f.locals.count(1) == 0, "test value slot unexpectedly initialized");
    const bool accepted = counted
        ? f.runCountedScalarLoop(0, 4, 1, 1, 9)
        : f.runSimpleScalarLoop(0, 4, 1, 1, OP_CONG);
    require(accepted, "zero-iteration scalar loop should be accepted");
    require(f.locals.count(1) == 0,
            "zero-iteration loop unexpectedly initialized an unused binding");
    const bool longLoop = counted
        ? f.runCountedScalarLoop(0, 5000, 1, 1, 9)
        : f.runSimpleScalarLoop(0, 5000, 1, 1, OP_CONG);
    require(!longLoop, "long loop with missing value must deopt");
    require(f.locals.count(1) == 0, "deopt unexpectedly initialized an unused binding");
}

void verifyDirectSlotAndReturnAbi() {
    Program program;
    Frame f(program);
    const int id = f.nativeId;
    f.push(777); // ABI peepholes must preserve unrelated stack entries.

    require(vpp_aot_store_integer(id, 5, -42) == 0, "integer slot store failed");
    require(f.load(5) == Value(-42), "integer slot store result differs");
    require(f.stack.size() == 1 && f.stack[0] == Value(777),
            "integer slot store changed operand stack");

    f.push(std::string("text"));
    require(vpp_aot_assign_slot(id, 6) == 0, "stack-to-slot store failed");
    require(f.load(6) == Value(std::string("text")), "string slot store differs");
    require(f.stack.size() == 1 && f.stack[0] == Value(777),
            "stack-to-slot store consumed another live value");

    auto captured = std::make_shared<vietvm::runtime::RuntimeCell>();
    captured->value = 123;
    f.locals[7] = captured;
    require(vpp_aot_return_slot(id, 7) == 0, "captured slot return failed");
    require(f.result == Value(123), "captured slot return differs");
    captured->value = 456;
    require(vpp_aot_return_slot(id, 7) == 0 && f.result == Value(456),
            "captured slot return ignored a shared-cell update");
    require(vpp_aot_return_integer(id, -2147483647-1) == 0 &&
            f.result == Value(-2147483647-1), "integer return differs");
    require(f.stack.size() == 1 && f.stack[0] == Value(777),
            "direct return changed live operand stack");

    require(vpp_aot_return_slot(id, 8) == 0 && f.result == Value(0),
            "uninitialized slot return differs from dynamic load");
    require(f.locals.count(8) == 1, "uninitialized return did not create slot");

    f.stack.clear();
    require(vpp_aot_assign_slot(id, 9) == 1 && program.pendingError,
            "missing stack value must raise a pending error");
    require(f.locals.count(9) == 0, "failing assignment created a slot");
    program.pendingError = {};
}

void verifyFloatingBinaryParity() {
    const Value inputs[] = {
        Value(2), Value(-7), Value(0), Value(2.25), Value(-0.0),
        Value(std::numeric_limits<double>::quiet_NaN()),
        Value(std::numeric_limits<double>::infinity())
    };
    const int operations[] = {
        OP_CONG, OP_TRU, OP_NHAN, OP_SO_SANH_BANG, OP_KHAC_BANG,
        OP_LON_HON, OP_NHO_HON, OP_LON_HON_HOAC_BANG,
        OP_NHO_HON_HOAC_BANG, OP_Logic_VA, OP_Logic_HOAC
    };
    Program program;
    Frame f(program);
    for (const auto &left : inputs) {
        for (const auto &right : inputs) {
            for (const int operation : operations) {
                f.push(left);
                f.push(right);
                f.binary(operation);
                const Value actual = f.pop();
                const Value expected = evaluateBinaryOperator(operation, left, right, 0);
                require(actual.index() == expected.index(),
                        "floating binary result type differs from VM");
                if (const auto *integer = std::get_if<int>(&actual))
                    require(*integer == std::get<int>(expected),
                            "floating comparison or boolean result differs from VM");
                else if (const auto *real = std::get_if<double>(&actual)) {
                    const double reference = std::get<double>(expected);
                    require((*real == reference ||
                             (std::isnan(*real) && std::isnan(reference))) &&
                            (reference != 0.0 || std::signbit(*real) == std::signbit(reference)),
                            "floating arithmetic differs from VM");
                } else require(false, "numeric binary unexpectedly produced object");
            }
        }
    }
    require(f.stack.empty(), "floating binary parity left values on stack");
}

void verifyFusedNumericSlotParity() {
    const Value inputs[] = {
        Value(-7), Value(0), Value(2), Value(std::numeric_limits<int>::max()),
        Value(0.0), Value(-0.0), Value(2.25),
        Value(std::numeric_limits<double>::quiet_NaN()),
        Value(std::numeric_limits<double>::infinity())
    };
    const int arithmetic[] = {OP_CONG, OP_TRU, OP_NHAN};
    const int comparisons[] = {
        OP_SO_SANH_BANG, OP_KHAC_BANG, OP_LON_HON,
        OP_NHO_HON, OP_LON_HON_HOAC_BANG, OP_NHO_HON_HOAC_BANG
    };
    Program program;
    Frame f(program);
    for (const Value &left : inputs) {
        for (const Value &right : inputs) {
            f.store(0, left);
            f.store(1, right);
            for (const int operation : arithmetic) {
                const Value expected = evaluateBinaryOperator(operation, left, right, 0);
                f.updateNumericSlots(0, 1, operation, 2);
                const Value actual = f.load(2);
                require(actual.index() == expected.index(), "fused numeric result type differs");
                if (const auto *integer = std::get_if<int>(&actual))
                    require(*integer == std::get<int>(expected), "fused integer differs");
                else if (const auto *real = std::get_if<double>(&actual)) {
                    const double reference = std::get<double>(expected);
                    require((*real == reference ||
                             (std::isnan(*real) && std::isnan(reference))) &&
                            (reference != 0.0 || std::signbit(*real) == std::signbit(reference)),
                            "fused float or mixed numeric differs");
                } else require(false, "fused numeric result was not a scalar");
            }
            for (const int operation : comparisons) {
                const Value expected = evaluateBinaryOperator(operation, left, right, 0);
                require(f.compareNumericSlots(0, 1, operation) ==
                        vietvm::runtime::stackValueTruthy(expected),
                        "fused numeric comparison differs");
            }
        }
    }
}

void verifyFrameCellReuseAndCaptureIsolation() {
    Program program;
    auto closure = std::make_shared<vietvm::runtime::RuntimeClosure>();
    {
        Frame first(program);
        first.store(5, 123);
        closure->captures[5] = first.cell(5);
    }
    // The next call must not reset or overwrite a captured binding, even
    // when its local slot number matches the previous function's slot.
    {
        Frame second(program);
        require(second.locals.empty(), "reused frame leaked prior locals");
        second.store(5, 456);
        require(second.load(5) == Value(456), "new frame local differs");
        require(second.cell(5) != closure->captures[5],
                "captured cell was incorrectly recycled");
    }
    require(closure->captures[5]->value == Value(123),
            "captured local changed after its original frame returned");

    {
        Frame first(program);
        first.stack.reserve(256);
        first.push(std::string("old temporary object"));
        first.push(42);
    }
    {
        Program other;
        Frame next(other);
        require(next.stack.empty(), "cached frame leaked an operand");
        require(next.stack.capacity() >= 256,
                "hot operand stack buffer was not reused");
        next.push(73);
        require(next.pop() == Value(73), "reused stack corrupted a scalar");
    }

    const vietvm::runtime::RuntimeCell *oldAddress = nullptr;
    {
        Frame first(program);
        first.store(9, std::string("temporary payload"));
        oldAddress = first.cell(9).get();
    }
    {
        Program other;
        Frame next(other);
        require(next.locals.empty(), "local map leaked into a different Program");
        next.store(9, 71);
        require(next.cell(9).get() == oldAddress,
                "exclusive local cell was not reused");
        require(next.load(9) == Value(71), "reused cell retained old value");
    }
    {
        Frame root(program, true);
        root.store(42, 900);
    }
    require(program.globals[42]->value == Value(900),
            "global binding was recycled with a local frame");
}

void verifyCachedBindingReplacements() {
    Program program;
    {
        Frame global(program, true);
        global.store(11, 123);
        require(global.valueAt(11) == Value(123), "global cache lookup differs");
    }
    {
        Frame local(program);
        require(local.valueAt(11) == Value(123), "global lookup inside frame differs");
        local.arguments = {Value(456)};
        local.parameter(11, 0, -1);
        require(local.load(11) == Value(456), "parameter failed to shadow cached global");
        local.initialize(11);
        require(local.load(11) == Value(0), "initialize retained overwritten cached binding");
        local.store(11, 99);
        require(local.valueAt(11) == Value(99), "cached store lost local binding");
        local.store(19, 77); // Collides in the eight-entry binding cache.
        require(local.valueAt(19) == Value(77) && local.valueAt(11) == Value(99),
                "binding cache collision corrupted values");
    }
    require(program.globals[11]->value == Value(123),
            "local cache modified a shadowed global");
}
} // namespace

int main() {
    try {
        compare(0, 0, 5, 7, OP_CONG);
        compare(4, 2, 5, 7, OP_CONG);
        compare(-3, 4, 5, 7, OP_CONG);
        compare(0, 256, 17, 3, OP_NHAN);
        compare(0, 4096, 17, 3, OP_CONG);
        compare(0, 4097, 17, 3, OP_CONG);
        compare(-2, 8193, 17, 3, OP_NHAN);
        compare(0, 50000, 0.125, 3, OP_CONG);
        compare(0, 4097, 0.25, 2, OP_NHAN);
        compare(0, 7, std::numeric_limits<int>::max(), 5, OP_CONG);
        compare(1, 9, 1, 3, OP_TRU);
        compare(0, 5, -2.5, 3, OP_CONG);
        compare(0, 6, 0.5, 2, OP_NHAN);
        compare(0, 6, std::numeric_limits<double>::quiet_NaN(), 3, OP_CONG);
        compare(0, 256, 17, 251, OP_CONG, true, 65521);
        compare(0, 256, 17.5, 251, OP_CONG, true, 65521);
        compare(0, 4097, 17, 251, OP_CONG, true, 65521);
        compare(0, 8193, 17.5, 251, OP_CONG, true, 65521);
        compare(0, 1, 1, 1, OP_CONG, true, 2);
        compare(0, 3, 1, 1, OP_CONG, true, 2);
        compare(-1, 0, 1, 2, OP_CONG, true, 7);
        compareNumericAdd(0,4097,0,0.25,0.0);
        compareNumericAdd(0,50000,0,0.25,0.0);
        compareNumericAdd(-2,8193,3,3,10);
        compareNumericAdd(0,8193,3,2.25,0.125);
        compareNumericAdd(0,100,0,1,0);
        compareNumericAdd(0,0,0,0.25,0.0);
        compareNumericAdd(10,5,0,0.25,0.0);
        compareNumericAdd(0,4097,3,0.25,0.125);
        compareNumericAdd(0,4097,3,2,17);
        compareNumericAdd(0,4097,0,0.25,0.0,true);
        compareNumericAdd(0,4097,0,5,0,true);
        compareNumericAdd(0,20,3,1,std::numeric_limits<int>::max());
        compareNumericAdd(0,10,0,std::numeric_limits<double>::quiet_NaN(),0.0);
        verifyNumericAddDeopt();
        deopt(0, 1'000'001, 5, OP_CONG);
        deopt(0, 2, std::string("text"), OP_CONG);
        deopt(0, 2, 5, OP_CHIA);
        aliasDeopt(false);
        aliasDeopt(true);
        absentValueRemainsAbsent(false);
        absentValueRemainsAbsent(true);
        verifyDirectSlotAndReturnAbi();
        verifyFloatingBinaryParity();
        verifyFusedNumericSlotParity();
        verifyFrameCellReuseAndCaptureIsolation();
        verifyCachedBindingReplacements();
        std::cout << "AOT scalar loops: optimized and dynamic stack/value parity PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "AOT scalar loop regression: " << error.what() << '\n';
        return 1;
    }
}
