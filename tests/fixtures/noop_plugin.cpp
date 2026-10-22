#include <windows.h>

// No work in DllMain. The smoke test explicitly calls this export in its own
// process; this fixture is never injected into another process.
extern "C" __declspec(dllexport) int FixtureValue() {
    return 42;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
