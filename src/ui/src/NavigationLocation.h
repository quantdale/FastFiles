#pragma once

#include <string>

namespace ffui {

// A typed navigation destination. FileSystem locations carry a canonical
// path; an empty path is the synthetic "This PC" root. Home is a distinct
// kind whose path is ALWAYS empty, so it can never be passed to a Win32
// filesystem API as a path. See openspec/changes/quick-access-home (D1/D2).
struct NavigationLocation {
    enum class Kind { FileSystem, Home };
    Kind kind = Kind::FileSystem;
    // Valid only when kind == FileSystem. When kind == FileSystem and path is
    // empty, the location is the synthetic "This PC" root. For kind == Home
    // this is ALWAYS empty (invariant enforced by Home()).
    std::wstring path;

    // quick-access-home 1.1: always produces a FileSystem location. An empty
    // path stays empty (meaning "This PC"); the kind is never Home.
    // NOTE: the Win32 extended-length prefix is deliberately PRESERVED here —
    // long-path navigation depends on it. Display surfaces strip it via
    // TabLabelForLocation / DisplayPathLeaf; never normalize in the model.
    static NavigationLocation FromPath(std::wstring path) {
        return NavigationLocation{Kind::FileSystem, std::move(path)};
    }
    // quick-access-home 1.1: produces the Home location with an ALWAYS-EMPTY
    // path. Home never carries a filesystem path.
    static NavigationLocation Home() {
        return NavigationLocation{Kind::Home, {}};
    }
    bool IsHome() const { return kind == Kind::Home; }
    bool IsFileSystem() const { return kind == Kind::FileSystem; }
    bool operator==(const NavigationLocation&) const = default;
};

} // namespace ffui
