if(NOT DEFINED VPP_EXEC OR VPP_EXEC STREQUAL "")
    message(FATAL_ERROR "VPP_EXEC is required")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
    message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}" "${TEST_ROOT}/du-an")
file(WRITE "${TEST_ROOT}/du-an/module.vi" "hàm ping() { trả về 7; }\n")

set(LSP_INPUT "${TEST_ROOT}/lsp-input.txt")
file(WRITE "${LSP_INPUT}" "")

function(append_lsp_message payload)
    string(LENGTH "${payload}" payload_length)
    file(APPEND "${LSP_INPUT}"
        "Content-Length: ${payload_length}\r\n\r\n${payload}")
endfunction()

set(uri "file:///lsp-semantic.vi")
set(source_text [=[hàm cộng(a, b) { trả về a + b; }\nhàm main() { x = cộng(1, 2); in x; }\n]=])
get_filename_component(import_main_path "${TEST_ROOT}/du-an/main.vi" ABSOLUTE)
file(TO_CMAKE_PATH "${import_main_path}" import_main_path)
if(WIN32)
    set(import_uri "file:///${import_main_path}")
else()
    set(import_uri "file://${import_main_path}")
endif()
string(REPLACE " " "%20" import_uri "${import_uri}")
set(import_source [=[nhập module.vi;\nhàm main() { in ping(); }\n]=])

append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\",\"languageId\":\"vpp\",\"version\":1,\"text\":\"${source_text}\"}}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\"},\"position\":{\"line\":1,\"character\":34}}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\"},\"position\":{\"line\":1,\"character\":18}}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\"},\"position\":{\"line\":1,\"character\":18}}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/rename\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\"},\"position\":{\"line\":1,\"character\":18},\"newName\":\"tổng\"}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/rename\",\"params\":{\"textDocument\":{\"uri\":\"${uri}\"},\"position\":{\"line\":1,\"character\":18},\"newName\":\"hàm\"}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${import_uri}\",\"languageId\":\"vpp\",\"version\":1,\"text\":\"${import_source}\"}}}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"shutdown\",\"params\":null}")
append_lsp_message(
    "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}")

execute_process(
    COMMAND "${VPP_EXEC}" --lsp
    WORKING_DIRECTORY "${TEST_ROOT}"
    INPUT_FILE "${LSP_INPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE lsp_output
    ERROR_VARIABLE lsp_error)

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "LSP thoát với mã ${result}\nstdout:\n${lsp_output}\nstderr:\n${lsp_error}")
endif()

function(require_output fragment description)
    string(FIND "${lsp_output}" "${fragment}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "LSP thiếu ${description}\nMẫu cần có:\n${fragment}\nOutput:\n${lsp_output}")
    endif()
endfunction()

require_output("\"completionProvider\":{\"triggerCharacters\":[\".\"]}"
               "capability completion")
require_output("\"definitionProvider\":true" "capability go-to-definition")
require_output("\"hoverProvider\":true" "capability hover")
require_output("\"renameProvider\":true" "capability rename")
require_output("\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"${uri}\",\"diagnostics\":[]}"
               "diagnostic rỗng cho source hợp lệ")
require_output("\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"${import_uri}\",\"diagnostics\":[]}"
               "diagnostic resolve import tương đối theo thư mục tài liệu")

require_output("\"label\":\"cộng\",\"kind\":3,\"detail\":\"hàm · 2..2 tham số\""
               "completion cho hàm semantic")
require_output("\"label\":\"x\",\"kind\":6,\"detail\":\"biến cục bộ\""
               "completion cho biến cục bộ")

require_output("\"id\":3,\"result\":{\"uri\":\"${uri}\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}}}"
               "definition trỏ đúng token khai báo tiếng Việt")
require_output("\"id\":4,\"result\":{\"contents\":{\"kind\":\"plaintext\",\"value\":\"hàm cộng — 2..2 tham số\"},\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}}}"
               "hover semantic có arity")

require_output("\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}},\"newText\":\"tổng\""
               "rename tại khai báo")
require_output("\"range\":{\"start\":{\"line\":1,\"character\":17},\"end\":{\"line\":1,\"character\":21}},\"newText\":\"tổng\""
               "rename tại điểm gọi theo UTF-16")
string(REGEX MATCHALL "\"newText\":\"tổng\"" rename_edits "${lsp_output}")
list(LENGTH rename_edits rename_edit_count)
if(NOT rename_edit_count EQUAL 2)
    message(FATAL_ERROR
        "rename phải chỉ sửa declaration + reference cùng SymbolId; nhận ${rename_edit_count} edits\n${lsp_output}")
endif()
require_output("\"id\":6,\"result\":null" "từ chối rename sang từ khóa")

message(STATUS "LSP semantic contract passed")
