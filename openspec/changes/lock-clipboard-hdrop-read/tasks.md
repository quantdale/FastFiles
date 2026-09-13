## 1. Locked clipboard read

- [ ] 1.1 In `ReadFilesFromClipboard`, `GlobalLock` the `CF_HDROP` HGLOBAL before `DragQueryFileW` and `GlobalUnlock` on every exit path (RAII). Failed lock returns an empty list (design D1/D2).
- [ ] 1.2 Lock and unlock the Preferred DropEffect HGLOBAL before reading the `DWORD`; failed lock leaves `cut == false` and still returns HDROP paths if that handle locked.
- [ ] 1.3 Confirm `HasFilesOnClipboard` does not dereference bytes without a lock (null-handle presence check may stay).

## 2. Tests

- [ ] 2.1 Extend `tests/ui/test_quick_actions.cpp` (or a sibling target) to round-trip HDROP + Preferred DropEffect through the real read function with lock pairing; assert lock failure yields empty paths rather than crashing.
- [ ] 2.2 Run `cmake --build --preset debug --target ffquick_actions_tests` and `ctest --test-dir build/debug -R ffquick_actions --output-on-failure`.
