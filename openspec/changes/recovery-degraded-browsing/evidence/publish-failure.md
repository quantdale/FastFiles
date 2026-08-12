# Publish-failure acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

## Source path

The additive `ffprotocol::DirectoryErrorReason::PublishFailed` value is present in `src/protocol/include/ffprotocol/UiProtocol.h`. `UiServer::RepublishAndBroadcastGeneration` returns failure; the request path sends `PublishFailed` to the requesting UI client, while watcher publication failures are broadcast to subscribed clients. `ColumnView` maps the protocol value to `SnapshotFailureReason::PublishFailed`, retaining same-context rows as stale-with-error and rendering the bounded failure message.

## Focused tests

```powershell
cmake --build --preset debug --target \
  ffengine_snapshot_publisher_tests ffsnapshot_freshness_tests \
  ffengine_ui_server_lifecycle_tests
ctest --test-dir build/debug -R \
  'ffengine_snapshot_publisher_tests|ffsnapshot_freshness_tests|ffengine_ui_server_lifecycle_tests' \
  --output-on-failure
```

All three focused tests passed:

- `ffengine_snapshot_publisher_tests`: publication above the old 8 MiB cliff succeeds; an oversized slot publication fails closed without advancing the generation.
- `ffsnapshot_freshness_tests`: same-context publish failure retains last-known-good data as stale, new contexts become unavailable, and a later generation clears the error.
- `ffengine_ui_server_lifecycle_tests`: normal watcher publication, blocked publication cancellation, downstream disconnect unwind, and idle shutdown all pass.

The final Debug/Analyze gates and serial CTest also passed 42/42 earlier in this campaign. No protocol value was removed or repurposed, and no privileged path was involved.
