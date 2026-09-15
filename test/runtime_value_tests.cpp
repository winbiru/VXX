#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "common/vm_native_collection_helpers.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_json_helpers.h"
#include "common/vm_native_text_helpers.h"
#include "common/vm_utils.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/text.h"
#include "vpp/runtime/collection.h"
#include "vpp/runtime/heap.h"
#include "vpp/runtime/object.h"

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
    expect(!sameStackValue(nullValue, make_int_value(0)),
           "null and numeric zero are different equality values");

    expect(!stackValueTruthy(make_int_value(0)) &&
               stackValueTruthy(make_int_value(-1)) &&
               !stackValueTruthy(make_float_value(0.0)) &&
               stackValueTruthy(make_float_value(0.5)) &&
               !stackValueTruthy(make_string_value("")) &&
               stackValueTruthy(make_string_value("x")) &&
               !stackValueTruthy(nullValue),
           "scalar truthiness follows the frozen runtime contract");
    expect(!stackValueTruthy(make_list_value({})) &&
               stackValueTruthy(make_list_value({make_int_value(1)})) &&
               !stackValueTruthy(make_map_value({})) &&
               stackValueTruthy(make_map_value(MapValue{{{"x", make_int_value(1)}}})),
           "collection truthiness depends on whether the collection is empty");

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

void testClosureValueAndGcSemantics() {
    using namespace vietvm::runtime;

    RuntimeHeap heap;
    RuntimeHeapScope heapScope(heap);
    ClosureHandle closure = std::make_shared<RuntimeClosure>();
    closure->functionId = 7;
    CellHandle cell = std::make_shared<RuntimeCell>();
    closure->captures[3] = cell;
    StackValue closureValue = make_closure_value(closure);
    cell->value = closureValue;

    expect(stackValueTruthy(closureValue) &&
               sameStackValue(closureValue, closureValue) &&
               sv_to_string(closureValue) == "<closure 7>",
           "closure values are truthy, use handle identity, and render stably");
    expect(heap.trackedObjectCount() == 1,
           "runtime heap tracks a closure allocation");

    closureValue = make_null_value();
    closure.reset();
    cell.reset();
    const RuntimeHeapStats stats = heap.collect({});
    expect(stats.swept == 1 && heap.trackedObjectCount() == 0,
           "tracing GC breaks an unreachable closure/capture-cell ownership cycle");
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

    int parsedInteger = 0;
    error.clear();
    expect(vietvm::helpers::parseIntArgFromStack(
               make_float_value(7.0), "thử số nguyên", "giá trị", parsedInteger, error) &&
               parsedInteger == 7,
           "native integer parser accepts integral floating-point values");
    error.clear();
    expect(vietvm::helpers::parseIntArgFromStack(
               make_string_value("7"), "thử số nguyên", "giá trị", parsedInteger, error) &&
               parsedInteger == 7,
           "native integer parser accepts complete decimal integer strings");
    error.clear();
    expect(!vietvm::helpers::parseIntArgFromStack(
               make_float_value(7.5), "thử số nguyên", "giá trị", parsedInteger, error) &&
               !error.empty(),
           "native integer parser rejects fractional floating-point values");
    error.clear();
    expect(!vietvm::helpers::parseIntArgFromStack(
               make_string_value("7.5"), "thử số nguyên", "giá trị", parsedInteger, error) &&
               !error.empty(),
           "native integer parser rejects strings with a fractional suffix");
}

void testTextIntegerContract() {
    StackValue result = make_null_value();
    std::string error;

    expect(vietvm::helpers::handleNativeTextFunction(
               "cắt chuỗi",
               {make_string_value(u8"Việt"), make_float_value(1.0), make_float_value(2.0)},
               result, error) &&
               error.empty() && std::holds_alternative<std::string>(result) &&
               std::get<std::string>(result) == u8"iệ",
           "string slice accepts integral floating-point offsets and counts");

    result = make_null_value();
    error.clear();
    expect(vietvm::helpers::handleNativeTextFunction(
               "cắt chuỗi",
               {make_string_value(u8"Việt"), make_float_value(1.5), make_int_value(2)},
               result, error) &&
               !error.empty(),
           "string slice rejects fractional offsets instead of truncating them");

    result = make_null_value();
    error.clear();
    expect(vietvm::helpers::handleNativeTextFunction(
               "mã hóa caesar",
               {make_string_value("Abc-Z"), make_float_value(2.0)},
               result, error) &&
               error.empty() && std::holds_alternative<std::string>(result) &&
               std::get<std::string>(result) == "Cde-B",
           "Caesar helper accepts an integral floating-point key");

    result = make_null_value();
    error.clear();
    expect(vietvm::helpers::handleNativeTextFunction(
               "mã hóa caesar",
               {make_string_value("Abc-Z"), make_float_value(2.5)},
               result, error) &&
               !error.empty(),
           "Caesar helper rejects a fractional key instead of truncating it");
}

void testHttpUrlValidationContract() {
    struct InvalidUrlCase {
        std::string url;
        std::string expectedReason;
    };

    const std::string malformedUtf8 =
        std::string("http://example.test/") + static_cast<char>(0xc0) +
        static_cast<char>(0xaf);
    const std::vector<InvalidUrlCase> cases = {
        {"", "URL rỗng"},
        {"ftp://example.test/file", "scheme http:// hoặc https://"},
        {"http://", "thiếu host"},
        {"https:///path", "thiếu host"},
        {"http://?x=1", "thiếu host"},
        {"http://#muc", "thiếu host"},
        {"http://:8080/path", "thiếu host"},
        {"http://example.test:/path", "port không hợp lệ"},
        {"http://example.test:99999/path", "port không hợp lệ"},
        {"http://2001:db8::1/path", "IPv6 phải đặt trong ngoặc vuông"},
        {"http://example.test/a b", "khoảng trắng hoặc ký tự điều khiển"},
        {malformedUtf8, "UTF-8 hợp lệ"},
    };

    for (const auto &testCase : cases) {
        StackValue result = make_null_value();
        std::string error;
        const bool handled = vietvm::helpers::runCurlHttpRequest(
            "GET", "mạng lấy", testCase.url, std::nullopt, result, error);
        expect(handled &&
                   error.find("mạng lấy: URL HTTP không hợp lệ") != std::string::npos &&
                   error.find(testCase.expectedReason) != std::string::npos,
               "HTTP client validates invalid URLs before invoking curl: " +
                   testCase.expectedReason);
    }
}

void testCollectionErrorContract() {
    StackValue result = make_null_value();
    std::string error;

    const bool overflowHandled = vietvm::helpers::handleNativeCollectionFunction(
        "tổng list",
        {make_list_value({make_int_value(std::numeric_limits<int>::max()), make_int_value(1)})},
        result,
        error);
    expect(overflowHandled && error.find("vượt phạm vi số nguyên") != std::string::npos,
           "collection sum rejects integer overflow instead of narrowing silently");

    result = make_null_value();
    error.clear();
    const bool mixedHandled = vietvm::helpers::handleNativeCollectionFunction(
        "sắp xếp",
        {make_list_value({make_int_value(1), make_string_value("hai")})},
        result,
        error);
    expect(mixedHandled && error.find("toàn số hoặc toàn chuỗi") != std::string::npos,
           "collection sort rejects mixed incomparable values deterministically");

    result = make_null_value();
    error.clear();
    const bool emptyMinHandled = vietvm::helpers::handleNativeCollectionFunction(
        "nhỏ nhất list", {make_list_value({})}, result, error);
    expect(emptyMinHandled && error.find("không nhận danh sách rỗng") != std::string::npos,
           "collection min rejects an empty list with a stable diagnostic");
}

void testJsonUtf8Contract() {
    const std::string malformed =
        std::string("bad-") + static_cast<char>(0xc0) + static_cast<char>(0xaf);

    StackValue parsed = make_null_value();
    std::string error;
    expect(!vietvm::helpers::parseJson(
               std::string("{\"tên\":\"") + malformed + "\"}", parsed, error) &&
               error.find("UTF-8 hợp lệ") != std::string::npos,
           "JSON parser rejects malformed UTF-8 before parsing string contents");

    error.clear();
    expect(vietvm::helpers::parseJson(u8"{\"tên\":\"Việt Nam 𠀀\"}", parsed, error) &&
               error.empty(),
           "JSON parser accepts valid Vietnamese UTF-8 and Unicode ngoài BMP");

    std::string encoded;
    error.clear();
    expect(!vietvm::helpers::stringifyJson(
               make_string_value(malformed), encoded, error) &&
               error.find("chuỗi phải là UTF-8 hợp lệ") != std::string::npos,
           "JSON serializer rejects malformed UTF-8 string values");

    MapValue invalidKeyMap;
    invalidKeyMap.entries[malformed] = make_int_value(1);
    encoded.clear();
    error.clear();
    expect(!vietvm::helpers::stringifyJson(
               make_map_value(std::move(invalidKeyMap)), encoded, error) &&
               error.find("khóa object phải là UTF-8 hợp lệ") != std::string::npos,
           "JSON serializer rejects malformed UTF-8 object keys");

    MapValue validMap;
    validMap.entries[u8"ngôn ngữ"] = make_string_value(u8"Tiếng Việt 𠀀");
    encoded.clear();
    error.clear();
    expect(vietvm::helpers::stringifyJson(
               make_map_value(std::move(validMap)), encoded, error) &&
               encoded == u8"{\"ngôn ngữ\":\"Tiếng Việt 𠀀\"}" && error.empty(),
           "JSON serializer preserves valid Vietnamese UTF-8 deterministically");
}

void testRuntimeObjectModel() {
    using namespace vietvm::runtime;

    const ClassHandle base = createClass("Base");
    const ClassHandle child = createClass("Child", base);
    expect(defineMethod(base, "speak", 11),
           "runtime class accepts a method binding");
    expect(defineMethod(child, "run", 12),
           "runtime subclass accepts its own method binding");

    const auto inherited = lookupMethod(child, "speak");
    expect(inherited.has_value() && *inherited == 11,
           "method lookup walks the superclass chain");
    const auto inheritedBinding = resolveMethod(child, "speak");
    expect(inheritedBinding.has_value() && inheritedBinding->functionId == 11 &&
               inheritedBinding->owner == base,
           "method resolution reports the class that owns an inherited method");
    expect(isSubclassOf(child, base) && !isSubclassOf(base, child),
           "runtime class hierarchy reports subclass relationships");

    expect(defineMethod(child, "speak", 13),
           "runtime subclass can override an inherited method");
    const auto overridden = lookupMethod(child, "speak");
    expect(overridden.has_value() && *overridden == 13,
           "method lookup prefers the nearest override");
    const auto overriddenBinding = resolveMethod(child, "speak");
    expect(overriddenBinding.has_value() && overriddenBinding->functionId == 13 &&
               overriddenBinding->owner == child,
           "method resolution reports the nearest overriding owner");

    const InstanceHandle first = createInstance(child);
    const InstanceHandle second = createInstance(child);
    expect(first != nullptr && setInstanceField(first, "answer", make_int_value(42)),
           "runtime instance stores a field value");
    const auto answer = getInstanceField(first, "answer");
    expect(answer.has_value() && sameStackValue(*answer, make_int_value(42)) &&
               hasInstanceField(first, "answer") &&
               !hasInstanceField(first, "missing"),
           "runtime instance field lookup preserves StackValue semantics");

    const StackValue firstValue = make_instance_value(first);
    const StackValue secondValue = make_instance_value(second);
    expect(sameStackValue(firstValue, firstValue) &&
               !sameStackValue(firstValue, secondValue),
           "runtime instances use identity equality");
    expect(sv_to_string(make_class_value(child)) == "<class Child>" &&
               sv_to_string(firstValue) == "<instance Child>",
           "runtime class and instance values have stable debug rendering");

    std::string jsonOutput;
    std::string jsonError;
    expect(!vietvm::helpers::stringifyJson(firstValue, jsonOutput, jsonError) &&
               !jsonError.empty(),
           "JSON conversion rejects runtime instances without treating them as maps");
}

void testSharedTextAndWireHelpers() {
    const std::vector<std::string> words = vietvm::core::splitAsciiWords("  một\thai\r\nba  ");
    expect(vietvm::core::joinWithSpaces(words) == "một hai ba",
           "ASCII whitespace helpers normalize all supported whitespace");
    expect(vietvm::core::toLowerAscii("AbC Đ") == "abc Đ" &&
               vietvm::core::toUpperAscii("aBc đ") == "ABC đ",
           "ASCII case helpers preserve UTF-8 bytes");
    expect(vietvm::core::toLowerUtf8Vietnamese(u8"ĐẶNG THỊ HỒNG 𠀀中") ==
               u8"đặng thị hồng 𠀀中" &&
               vietvm::core::toUpperUtf8Vietnamese(u8"Việt Nam ươ 𠀀中") ==
               u8"VIỆT NAM ƯƠ 𠀀中",
           "Vietnamese UTF-8 case conversion covers precomposed letters and preserves other scripts");
    const std::string vietnameseLower =
        u8"ăâêôơưđàáảãạằắẳẵặầấẩẫậèéẻẽẹềếểễệìíỉĩịòóỏõọồốổỗộờớởỡợùúủũụừứửữựỳýỷỹỵ";
    const std::string vietnameseUpper =
        u8"ĂÂÊÔƠƯĐÀÁẢÃẠẰẮẲẴẶẦẤẨẪẬÈÉẺẼẸỀẾỂỄỆÌÍỈĨỊÒÓỎÕỌỒỐỔỖỘỜỚỞỠỢÙÚỦŨỤỪỨỬỮỰỲÝỶỸỴ";
    expect(vietvm::core::toUpperUtf8Vietnamese(vietnameseLower) == vietnameseUpper &&
               vietvm::core::toLowerUtf8Vietnamese(vietnameseUpper) == vietnameseLower,
           "Vietnamese UTF-8 case conversion covers every precomposed Vietnamese case pair");
    expect(vietvm::core::normalizeUtf8VietnameseNfc(u8"Vie\u0323\u0302t Nam") ==
               u8"Việt Nam" &&
               vietvm::core::normalizeUtf8VietnameseNfc(u8"A\u0306\u0301 O\u031B\u0301") ==
               u8"Ắ Ớ" &&
               vietvm::core::normalizeUtf8VietnameseNfc(u8"Việt 𠀀中") == u8"Việt 𠀀中",
           "Vietnamese NFC normalization composes decomposed marks and preserves other scripts");
    const std::string vietnameseLowerNfd =
        u8"a\u0306a\u0302e\u0302o\u0302o\u031Bu\u031B\u0111a\u0300a\u0301a\u0309a\u0303a\u0323"
        u8"a\u0306\u0300a\u0306\u0301a\u0306\u0309a\u0306\u0303a\u0323\u0306"
        u8"a\u0302\u0300a\u0302\u0301a\u0302\u0309a\u0302\u0303a\u0323\u0302"
        u8"e\u0300e\u0301e\u0309e\u0303e\u0323e\u0302\u0300e\u0302\u0301e\u0302\u0309e\u0302\u0303e\u0323\u0302"
        u8"i\u0300i\u0301i\u0309i\u0303i\u0323"
        u8"o\u0300o\u0301o\u0309o\u0303o\u0323o\u0302\u0300o\u0302\u0301o\u0302\u0309o\u0302\u0303o\u0323\u0302"
        u8"o\u031B\u0300o\u031B\u0301o\u031B\u0309o\u031B\u0303o\u031B\u0323"
        u8"u\u0300u\u0301u\u0309u\u0303u\u0323u\u031B\u0300u\u031B\u0301u\u031B\u0309u\u031B\u0303u\u031B\u0323"
        u8"y\u0300y\u0301y\u0309y\u0303y\u0323";
    expect(vietvm::core::normalizeUtf8VietnameseNfc(vietnameseLowerNfd) ==
               vietnameseLower,
           "Vietnamese NFC normalization covers the complete lowercase Vietnamese corpus");
    expect(vietvm::core::countVietnameseNormalizedSubstring(
               u8"Vie\u0323\u0302t Vie\u0323\u0302t", u8"ệ") == 2 &&
               vietvm::core::containsVietnameseNormalizedSubstring(
                   u8"Vie\u0323\u0302t Nam", u8"Việt") &&
               vietvm::core::replaceVietnameseNormalizedAll(
                   u8"Vie\u0323\u0302t Nam", u8"Việt", u8"Đại Việt") ==
                   u8"Đại Việt Nam",
           "Vietnamese substring helpers treat NFC and decomposed input equivalently");
    const std::string mixedUtf8 = u8"Aệ中𠀀";
    expect(vietvm::core::isValidUtf8(mixedUtf8) &&
               vietvm::core::utf8CodePointCount(mixedUtf8) == 4,
           "UTF-8 helpers validate and count Unicode code points");
    expect(vietvm::core::utf8CodePointAt(mixedUtf8, 0) == std::optional<std::string>("A") &&
               vietvm::core::utf8CodePointAt(mixedUtf8, 1) == std::optional<std::string>(u8"ệ") &&
               vietvm::core::utf8CodePointAt(mixedUtf8, 2) == std::optional<std::string>(u8"中") &&
               vietvm::core::utf8CodePointAt(mixedUtf8, 3) == std::optional<std::string>(u8"𠀀") &&
               !vietvm::core::utf8CodePointAt(mixedUtf8, 4).has_value(),
           "UTF-8 code-point indexing returns complete multibyte units");
    expect(vietvm::core::utf8CodePointSlice(u8"𠀀Việt Nam", 1, 4) == u8"Việt" &&
               vietvm::core::utf8CodePointSlice(u8"𠀀Việt", 0, 2) == u8"𠀀V" &&
               vietvm::core::utf8CodePointSlice(u8"Việt", 2, 99) == u8"ệt" &&
               vietvm::core::utf8CodePointSlice(u8"Việt", 4, 1).empty(),
           "UTF-8 slicing uses code-point offsets and clamps at the end");
    const std::string malformed = std::string("A") + static_cast<char>(0xc0) +
                                  static_cast<char>(0xaf) + "B";
    expect(!vietvm::core::isValidUtf8(malformed) &&
               vietvm::core::utf8CodePointCount(malformed) == 4 &&
               vietvm::core::utf8CodePointAt(malformed, 1) ==
                   std::optional<std::string>(std::string(1, static_cast<char>(0xc0))),
           "malformed UTF-8 is detectable while raw-byte fallback remains deterministic");
    expect(vietvm::core::toUpperUtf8Vietnamese(malformed) ==
               std::string("A") + static_cast<char>(0xc0) +
                   static_cast<char>(0xaf) + "B",
           "Vietnamese case conversion preserves malformed raw bytes");
    expect(vietvm::core::normalizeUtf8VietnameseNfc(malformed) == malformed,
           "Vietnamese NFC normalization preserves malformed raw bytes");
    expect(vietvm::core::utf8CodePointSlice(malformed, 1, 2) ==
               std::string(1, static_cast<char>(0xc0)) + static_cast<char>(0xaf),
           "UTF-8 slicing preserves malformed raw-byte fallback units");
    expect(vietvm::core::countSubstring("aaaa", "aa") == 2,
           "substring counting keeps non-overlapping native semantics");
    expect(vietvm::core::replaceAll("a-b-a", "a", "x") == "x-b-x",
           "replace-all helper preserves native left-to-right replacement");
    expect(vietvm::core::longestAsciiWord("mot haiiii ba") == "haiiii",
           "longest-word helper shares ASCII whitespace tokenization");
    expect(vietvm::core::titleVietnameseWords(u8"đẶNG   thỊ hỒNG") ==
               u8"Đặng Thị Hồng" &&
               vietvm::core::titleVietnameseWords(u8"đa\u0323\u0306ng thi\u0323") ==
                   u8"Đặng Thị",
           "title helper normalizes Vietnamese NFC and case per word");
    expect(vietvm::core::isVietnameseCaseInsensitivePalindrome(u8"ĐỏRỏđ") &&
               vietvm::core::isVietnameseCaseInsensitivePalindrome(
                   u8"A\u0306\u0301bBắ"),
           "palindrome helper shares Vietnamese NFC and case normalization");
    expect(vietvm::core::areVietnameseAnagrams(
               u8"Việt Nam", u8"Nam Vie\u0323\u0302t") &&
               vietvm::core::areVietnameseAnagrams("Dormitory", "Dirty room"),
           "anagram helper compares normalized Vietnamese code points and ignores whitespace");
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
    expect(sv_to_string(evaluateBinaryOperator(
               OP_SO_SANH_BANG, make_null_value(), make_null_value(), 0)) == "1" &&
               sv_to_string(evaluateBinaryOperator(
                   OP_KHAC_BANG, make_null_value(), make_int_value(0), 0)) == "1",
           "shared binary evaluator uses StackValue equality for null and mixed types");
    const StackValue list = make_list_value({make_int_value(1)});
    expect(sv_to_string(evaluateBinaryOperator(
               OP_SO_SANH_BANG, list, list, 0)) == "1" &&
               sv_to_string(evaluateBinaryOperator(
                   OP_SO_SANH_BANG, list, make_list_value({make_int_value(1)}), 0)) == "0",
           "shared binary evaluator keeps reference identity equality");
    expect(sv_to_string(evaluateBinaryOperator(
               OP_Logic_VA, make_string_value("x"), make_list_value({make_int_value(1)}), 0)) == "1" &&
               sv_to_string(evaluateBinaryOperator(
                   OP_Logic_HOAC, make_string_value(""), make_null_value(), 0)) == "0",
           "logical operators share branch truthiness for every runtime value kind");

    bool rejectedMixedOrdering = false;
    try {
        (void)evaluateBinaryOperator(
            OP_NHO_HON, make_int_value(1), make_string_value("2"), 0);
    } catch (const std::runtime_error &) {
        rejectedMixedOrdering = true;
    }
    expect(rejectedMixedOrdering,
           "ordering rejects values without a shared numeric or string ordering");
    expect(sv_to_string(evaluateModuloOperator(
               make_int_value(7), make_int_value(3), OP_MODULO, 0)) == "1",
           "shared modulo evaluator preserves integer modulo");
    expect(sv_to_string(evaluateModuloOperator(
               make_float_value(7.0), make_float_value(3.0), OP_MODULO, 0)) == "1",
           "shared modulo evaluator accepts integral floating-point values");
    bool rejectedFractionalModulo = false;
    try {
        (void)evaluateModuloOperator(
            make_float_value(7.5), make_int_value(3), OP_MODULO, 0);
    } catch (const std::runtime_error &error) {
        rejectedFractionalModulo = std::string(error.what()).find("số nguyên") != std::string::npos;
    }
    expect(rejectedFractionalModulo,
           "shared modulo evaluator rejects fractional values instead of truncating them");
}

} // namespace

int main() {
    try {
        testSharedStackValueSemantics();
        testClosureValueAndGcSemantics();
        testSharedNativeValidation();
        testTextIntegerContract();
        testHttpUrlValidationContract();
        testCollectionErrorContract();
        testJsonUtf8Contract();
        testRuntimeObjectModel();
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
