#pragma once

namespace ffindexsvc {

// Task 7.1: empirically verify that SeBackupPrivilege alone (the only
// privilege the service's virtual account is granted -- design.md D4) is
// sufficient to open a raw volume handle and query the USN journal, since
// this underpins the whole privilege-minimization design even though the
// real scan/journal logic is stubbed in this change (task 3.8).
//
// Deliberately runs as a startup self-check baked into the shipped
// service, rather than a one-off manual tool: verifying this once in a
// developer's sandbox wouldn't prove anything about the production
// deployment (running under the actual virtual service account with
// exactly one privilege), so this checks it every time the real,
// installed service starts. Logs the result; a negative result does not
// fail service startup in this change, since nothing yet depends on it
// succeeding (the follow-up change that implements real scanning is where
// this assumption becomes load-bearing).
void VerifyBackupPrivilegeSufficiency();

} // namespace ffindexsvc
