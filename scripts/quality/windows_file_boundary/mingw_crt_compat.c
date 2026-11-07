// Test-only compatibility with the legacy MSVCRT import library used by
// MinGW. Native Windows/MSVC resolves __p___argc from its UCRT normally.
// Keep this separate from probe.c to avoid the MinGW dllimport declaration.
int vpp_mingw_probe_argc;
int *__p___argc(void) { return &vpp_mingw_probe_argc; }
