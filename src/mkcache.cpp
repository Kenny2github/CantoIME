// mkcache.cpp
//
// canto-mkcache.exe [--per-user] <dictionary.txt> [<dictionary2.txt> ...]
//
// Parses each dictionary and writes its page-aligned mmap cache (see
// Dictionary.cpp for the format), so the IME maps the cache instead of parsing
// text. A no-op for a dictionary whose cache is already valid.
//
//   default      machine-wide: writes "<file>.cache" next to the dictionary
//                (the installer uses this for the stock dictionaries).
//   --per-user   writes only under %LOCALAPPDATA%\CantoIME (the options dialog
//                uses this for custom, per-user dictionaries).
//
// Exit code 0 if every dictionary ended up with a valid cache, 1 otherwise,
// 2 for usage.

#include "Dictionary.h"

#include <cwchar>

int wmain(int argc, wchar_t** argv)
{
    bool machineWide = true;
    bool any = false;
    bool allOk = true;

    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--per-user") == 0)
        {
            machineWide = false;
            continue;
        }
        any = true;
        allOk &= Dictionary::PrebuildCache(argv[i], machineWide);
    }

    if (!any)
        return 2;
    return allOk ? 0 : 1;
}
