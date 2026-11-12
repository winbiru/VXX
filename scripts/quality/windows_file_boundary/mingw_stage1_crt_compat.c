// MinGW's legacy MSVCRT library does not expose the UCRT __p___argc entry.
// Derive argc from the CRT-owned argv table for executable bootstrap tests.
// Native UCRT builds resolve __p___argc directly and do not need this shim.
extern char ***__p___argv(void);

int *__p___argc(void) {
    static int count;
    char ***table = __p___argv();
    count = 0;
    if (table == 0 || *table == 0)
        return &count;
    while (count < 4096 && (*table)[count] != 0)
        ++count;
    return &count;
}
