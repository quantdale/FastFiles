#pragma once

#include <cstdint>

namespace ffui {

// active-path-runtime-resilience 4.1: a small pure identity gate for work
// that may finish after its query, source connection, or snapshot context has
// changed.  The owner thread advances the gate; workers carry the returned
// token and the owner accepts a completion only when both identities match.
struct AsyncResultToken {
    uint64_t request = 0;
    uint64_t source = 0;

    friend constexpr bool operator==(const AsyncResultToken& left,
                                    const AsyncResultToken& right) noexcept {
        return left.request == right.request && left.source == right.source;
    }
};

class AsyncResultGate {
public:
    AsyncResultToken Begin(uint64_t source) noexcept {
        current_.source = source;
        current_.request = NextRequest(current_.request);
        return current_;
    }

    AsyncResultToken Invalidate(uint64_t source) noexcept {
        return Begin(source);
    }

    bool Accept(const AsyncResultToken& token) const noexcept {
        return token.request != 0 && token == current_;
    }

    AsyncResultToken Current() const noexcept { return current_; }

private:
    static uint64_t NextRequest(uint64_t current) noexcept {
        ++current;
        return current == 0 ? 1 : current;
    }

    AsyncResultToken current_{};
};

} // namespace ffui
