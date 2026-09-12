#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "common/vm_native_helpers.h"
#include "common/vm_utils.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/text.h"
#include "vpp/runtime/collection.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testSharedStackValueSemantics() {
    const StackValue one = make_int_value(1);
    const StackValue oneFloat = make_float_value(1.0);
    const StackValue nullValue = make_null_value();
    const ScalarValue wholeFloat = 2.0;
    expect(scalar_to_string(wholeFloat) == "2.0" &&
               sv_to_string(make_float_value(2.0)) == "2.0",
           "scalar and stack float formatting share the same whole-number spelling");
    expect(sameStackValue(one, oneFloat), "numeric collection equality promotes int and float");
    expect(sameStackValue(nullValue, make_null_value()), "null values compare equal");

    const StackValue firstList = make_list_value({make_int_value(1)});
    const StackValue secondList = make_list_value({make_int_value(1)});
    expect(!sameStackValue(firstList, secondList), "separate collection handles retain identity equality");
    expect(sameStackValue(firstList, firstList), "the same collection handle compares equal");

    bool comparable = false;
    expect(stackValueLess(make_int_value(1), make_float_value(2.0), comparable) && comparable,
           "numeric ordering is shared across int and float");
    (void)stackValueLess(make_int_value(1), make_string_value("1"), comparable);
    expect(!comparable, "mixed scalar values are not orderable");

    const std::vector<StackValue> unique = vietvm::runtime::uniqueStackValues(
        {make_int_value(1), make_float_value(1.0), make_null_value(),
         make_null_value(), make_string_value("x")});
    expect(sv_to_string(make_list_value(unique)) == "[1, rỗng, x]",
           "unique values preserve first appearance and shared equality");

    const std::vector<StackValue> left = {
        make_int_value(1), make_int_value(2), make_int_value(2)};
    const std::vector<StackValue> right = {
        make_float_value(2.0), make_int_value(3)};
    expect(vietvm::runtime::containsStackValue(left, make_float_value(1.0)),
           "collection membership reuses numeric StackValue equality");
    const auto foundIndex = vietvm::runtime::findStackValueIndex(left, make_float_value(2.0));
    expect(foundIndex.has_value() && *foundIndex == 1,
           "collection index search returns the first shared-equality match");
    expect(sv_to_string(make_list_value(vietvm::runtime::unionStackValues(left, right))) ==
               "[1, 2, 3]",
           "collection union preserves first appearance across both inputs");
    expect(sv_to_string(make_list_value(vietvm::runtime::intersectStackValues(left, right))) ==
               "[2]",
           "collection intersection is unique and preserves left-side order");
    expect(vietvm::runtime::areStackValueCollectionsDisjoint(
               left, {make_int_value(9), make_string_value("x")}),
           "collection disjoint helper reports independent value sets");
    expect(vietvm::runtime::isUniformSortableStackValues(
               {make_int_value(1), make_float_value(2.0)}) &&
               vietvm::runtime::isUniformSortableStackValues(
                   {make_string_value("a"), make_string_value("b")}) &&
               !vietvm::runtime::isUniformSortableStackValues(
                   {make_int_value(1), make_string_value("b")}),
           "sortable collection classification accepts only uniform numeric or text values");

    MapValue renderedMap;
    renderedMap.entries["text"] = make_string_value("xin chao");
    expect(sv_to_string(make_map_value(std::move(renderedMap))) ==
               "{\"text\": \"xin chao\"}",
           "map rendering preserves quoted scalar-string values");

    const StackValue cyclicList = make_list_value({});
    std::get<ListHandle>(cyclicList)->elements.push_back(cyclicList);
    expect(sv_to_string(cyclicList) == "[<cycle>]",
           "self-referential lists render without unbounded recursion");

    const StackValue cyclicMap = make_map_value({});
    std::get<MapHandle>(cyclicMap)->entries["self"] = cyclicMap;
    expect(sv_to_string(cyclicMap) == "{\"self\": <cycle>}",
           "self-referential maps render without unbounded recursion");

    // These cycles are created intentionally to exercise cycle-safe rendering.
    // Break the shared_ptr ownership cycles before leaving the test so leak
    // sanitizers can verify the runtime-value test without reporting the test
    // fixtures themselves as leaked allocations.
    std::get<ListHandle>(cyclicList)->elements.clear();
    std::get<MapHandle>(cyclicMap)->entries.clear();
}

void testSharedNativeValidation() {
    std::string error;
    ListHandle list;
    const std::vector<StackValue> listArgs = {make_list_value({make_int_value(2)})};
    expect(vietvm::helpers::getFirstListArgument(listArgs, "thử danh sách", list, error) &&
               list != nullptr && list->elements.size() == 1,
           "list validator returns a live handle");

    int index = -1;
    expect(vietvm::helpers::getNonNegativeListIndex(make_int_value(0), index, error) && index == 0,
           "list index validator accepts zero");
    expect(!vietvm::helpers::getNonNegativeListIndex(make_int_value(-1), index, error) &&
               error == "chỉ số danh sách vượt phạm vi",
           "list index validator retains negative-index diagnostic");

    MapValue map;
    map.entries["x"] = make_int_value(1);
    MapHandle mapHandle;
    expect(vietvm::helpers::getFirstMapArgument({make_map_value(map)}, "thử ánh xạ", mapHandle, error) &&
               mapHandle != nullptr && mapHandle->entries.size() == 1,
           "map validator returns a live handle");
    expect(vietvm::helpers::nativeArgumentCountError("f", 2).find("f") != std::string::npos,
           "native argument count formatter is shared");

    error = "giữ nguyên";
    expect(vietvm::helpers::requireNativeArgumentCount(listArgs, "thử số đối số", 1, error) &&
               error == "giữ nguyên",
           "native argument count validator accepts exact arity without changing prior error");
    const std::string expectedArityError =
        vietvm::helpers::nativeArgumentCountError("thử số đối số", 2);
    expect(!vietvm::helpers::requireNativeArgumentCount(listArgs, "thử số đối số", 2, error) &&
               error == expectedArityError,
           "native argument count validator retains the standard diagnostic");
}

void testSharedTextAndWireHelpers() {
    const std::vector<std::string> words = vietvm::core::splitAsciiWords("  một\thai\r\nba  ");
    expect(vietvm::core::joinWithSpaces(words) == "một hai ba",
           "ASCII whitespace helpers normalize all supported whitespace");
    expect(vietvm::core::toLowerAscii("AbC Đ") == "abc Đ" &&
               vietvm::core::toUpperAscii("aBc đ") == "ABC đ",
           "ASCII case helpers preserve UTF-8 bytes");
    expect(vietvm::core::countSubstring("aaaa", "aa") == 2,
           "substring counting keeps non-overlapping native semantics");
    expect(vietvm::core::replaceAll("a-b-a", "a", "x") == "x-b-x",
           "replace-all helper preserves native left-to-right replacement");
    expect(vietvm::core::longestAsciiWord("mot haiiii ba") == "haiiii",
           "longest-word helper shares ASCII whitespace tokenization");
    expect(vietvm::core::titleAsciiWords("hELLo   wORLD") == "Hello World",
           "title helper normalizes words through shared ASCII case functions");
    expect(vietvm::core::isAsciiCaseInsensitivePalindrome("AbBa"),
           "palindrome helper shares ASCII case normalization");
    expect(vietvm::core::areAsciiAnagrams("Dormitory", "Dirty room"),
           "anagram helper shares ASCII case and whitespace normalization");
    expect(vietvm::core::caesarAscii("Az-z", -1) == "Zy-y",
           "Caesar helper normalizes negative shifts");

    const std::string raw = std::string("\\\n\r\t") +
                            vietvm::bytecode::kLiteralRecordSeparator +
                            vietvm::bytecode::kLiteralFieldSeparator;
    const std::string encoded = vietvm::bytecode::escapeLiteralWireField(raw);
    expect(vietvm::bytecode::unescapeLiteralWireField(encoded) == raw,
           "literal wire escaping round-trips separators and control bytes");

    const auto property = vietvm::helpers::parsePropertyAssignment("  mode = demo  ");
    expect(property.has_value() && property->first == "mode" && property->second == "demo",
           "property parser trims a reusable assignment record");
    expect(!vietvm::helpers::parsePropertyAssignment(" # comment").has_value(),
           "property parser ignores comments");
}

void testSharedOperatorEvaluation() {
    expect(sv_to_string(evaluateBinaryOperator(
               OP_CONG, make_int_value(2), make_float_value(0.5), 0)) == "2.5",
           "shared binary evaluator promotes numeric addition");
    expect(sv_to_string(evaluateBinaryOperator(
               OP_CONG, make_string_value("a"), make_int_value(2), 0)) == "a2",
           "shared binary evaluator preserves string concatenation");
    expect(sv_to_string(evaluateBinaryOperator(
               OP_SO_SANH_BANG, make_int_value(1), make_float_value(1.0), 0)) == "1",
           "shared binary evaluator preserves numeric comparison");
    expect(sv_to_string(evaluateModuloOperator(
               make_int_value(7), make_int_value(3), OP_MODULO, 0)) == "1",
           "shared modulo evaluator preserves integer modulo");
}

} // namespace

int main() {
    try {
        testSharedStackValueSemantics();
        testSharedNativeValidation();
        testSharedTextAndWireHelpers();
        testSharedOperatorEvaluation();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: runtime value helper raised an exception: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " runtime value helper test(s) failed\n";
        return 1;
    }

    std::cout << "Runtime value helper tests passed\n";
    return 0;
}
