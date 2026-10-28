# Generated transfer result protection — provisional qualification

Base: freshly fetched PR32 source `12b40ee402582f27f1e21e9df44fffc8bebe51e3`. Original main checkout and its three dirty files preserved. Existing PRs stay unmerged.

## Confirmed defect
Old actual worker Move returns two completions/zero failures after overwriting the earlier incoming result. Shell generated `first - Copy.txt`; the fixture discovers this name through a separate native operation instead of assuming localization. Occupied-target control retains both transfers. Vacated-target adverse case loses firstIncomingCopies (0), fails two assertions, and cleans its unique temporary tree normally. See observed-native-overlap.log.gz. Only generated owned fixture files were touched.

## Repair and focused evidence
Before approved replacements execute, compare queued targets with callback-observed completed ordinary result paths. Refuse the whole queued replacement group on an overlapping namespace, report ERROR_ALREADY_EXISTS per queued replacement, and preserve replacement sources. No fresh existence check is presented as atomic protection.

Focused3/3PASS (3.19s): pure policy; previous native consent/cancellation suite; native new observed-name protection suite. New suite exercises Copy, Move and holding-folder Restore, each occupied control, vacated overlap and two-replacement refusal. Exact incoming bytes, source/target sentinels, one terminal event, completed/failure/skip counts and normal cleanup are asserted. Holding-folder Restore is not Recycle Bin Undo certification. Path tests include ordinal case, equality, either ancestor direction, sibling prefix, lexical dot segments, trailing separators, extended drive/UNC and empty observed names.

No changed visual surface; screenshots cannot certify this invisible worker boundary. No new privileged/Explorer drag/physical display qualification. Cross-product overlap cost on extreme mixed batches, filesystem aliases and missing callbacks remain unmeasured/unqualified. Fresh independent source review is in review.md. Full exact committed-source and combined qualification are pending task2.1/2.2.
