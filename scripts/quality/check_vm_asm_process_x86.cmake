# Compatibility entrypoint for the retired C cross-link harness. The current
# V++ process phase checks spawn/pipes/env/deadlines and raw ASM validation on
# the host architecture. Cross-architecture coverage comes from the CI matrix.
message(STATUS "Process ABI: using the V++/ASM host process regression phase")
set(VPP_VM_PHASE process)
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm.cmake")
