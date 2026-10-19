# Verify compiler-emitted ASM is accepted by Clang's real assembler.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_CLANG OR NOT DEFINED VPP_LEAF OR
    NOT DEFINED VPP_GENERAL OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Native ASM: missing inputs")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
foreach(case IN ITEMS
    "i386-unknown-linux-gnu#${VPP_LEAF}#tra42"
    "x86_64-unknown-linux-gnu#${VPP_LEAF}#cong"
    "aarch64-unknown-linux-gnu#${VPP_LEAF}#tru"
    "x86_64-pc-windows-msvc#${VPP_LEAF}#cong"
    "aarch64-unknown-linux-gnu#${VPP_GENERAL}#tong"
    "x86_64-unknown-linux-gnu#${VPP_GENERAL}#tinh")
    string(REPLACE "#" ";" parts "${case}")
    list(GET parts 0 target)
    list(GET parts 1 source)
    list(GET parts 2 name)
    set(asm "${VPP_OUTPUT_DIR}/${target}-${name}.s")
    set(obj "${VPP_OUTPUT_DIR}/${target}-${name}.o")
    execute_process(COMMAND "${VPP_EXEC}" build --target "${target}" --emit-asm
        "${name}" "${source}" "${asm}" RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ASM emit failed for ${target}/${name}: ${errors}")
    endif()
    execute_process(COMMAND "${VPP_CLANG}" -target "${target}" -c "${asm}" -o "${obj}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0 OR NOT EXISTS "${obj}")
        message(FATAL_ERROR "Clang rejected V++ ASM ${target}/${name}: ${errors}")
    endif()
endforeach()
message(STATUS "V++ assembly accepted by Clang for 4 targets: PASS")
