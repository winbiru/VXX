if (NOT DEFINED VPP_EXEC)
    message(FATAL_ERROR "VPP_EXEC missing")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" --list-targets
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "--list-targets failed: ${error}")
endif()
foreach(arch IN ITEMS x86_64 aarch64 riscv64 armv7 i386 powerpc64le mips64el loongarch64 s390x sparc64)
    if (NOT output MATCHES "${arch}-unknown-linux-gnu")
        message(FATAL_ERROR "--list-targets missing architecture ${arch}")
    endif()
endforeach()

execute_process(
    COMMAND "${VPP_EXEC}" build --target riscv64-unknown-linux-gnu ignored.vi
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if (result EQUAL 0 OR NOT error MATCHES "Native AOT.*riscv64-unknown-linux-gnu")
    message(FATAL_ERROR "build --target must reject unsupported AOT with diagnostic: ${error}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target riscv128-linux-gnu ignored.vi
    RESULT_VARIABLE result
    ERROR_VARIABLE error)
if (result EQUAL 0 OR NOT error MATCHES "Target không hợp lệ")
    message(FATAL_ERROR "build --target must reject unknown CPUs with diagnostic: ${error}")
endif()
