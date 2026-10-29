## Context

PeekMessage can return WM_QUIT regardless of requested message range. A pending PostQuitMessage may not materialize while unrelated posted messages remain, so an initial fixture with unrelated messages masked the defect. With a quiet queue, the exact legacy drain consumes quit as a payload. Parent destruction posts quit before child NCDESTROY drains, matching the observed runtime phase frontier without proving original message consumption.

## Decisions

Use one internal header-only DrainOwnedMessages helper for all removing payload drains in UI sources. Recognize WM_QUIT before interpreting LPARAM, retain its exit code, finish owned disposal, then repost once after the loop to avoid consuming our own repost indefinitely. Existing producer shutdown/lifetime locks and callback ownership remain unchanged. Worker queue initialization uses PM_NOREMOVE and is not changed. Disposers are the existing nonthrowing delete/cancel-and-release operations.

## Validation

Actual same-thread message-only windows: two owned payloads, unrelated window/message preservation, quiet-queue zero/nonzero quit, repeated drains and actual outer GetMessage. Legacy observer replaces delete only to safely expose message misclassification, not product behavior; --legacy must fail. Initial mistaken test ordering and intermediate masked results are retained ignored. Native full app close and broad certification remain separate pending work.
