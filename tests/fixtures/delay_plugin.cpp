// Compile-only input for static PE inspection; this DLL is never loaded by tests.
extern "C" __declspec(dllimport) int FixtureValue();
extern "C" __declspec(dllexport) int DelayedFixtureValue() {
    return FixtureValue();
}
