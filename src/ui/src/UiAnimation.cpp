#include "UiAnimation.h"

#include <algorithm>
#include <windows.h>

namespace ffui {

bool SystemAnimationsEnabled() {
    BOOL value = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &value, 0) != 0) {
        return value != FALSE;
    }
    return true;
}

void FloatAnimation::AnimateTo(float target, float durationMs, uint64_t nowMs) {
    target_ = target;
    if (!SystemAnimationsEnabled() || durationMs <= 0.0f) {
        Snap(target);
        return;
    }
    startValue_ = value_;
    startMs_ = nowMs;
    durationMs_ = durationMs;
    animating_ = true;
}

bool FloatAnimation::Tick(uint64_t nowMs) {
    if (!animating_) {
        return false;
    }
    const uint64_t elapsed = nowMs - startMs_;
    float t = static_cast<float>(elapsed) / durationMs_;
    t = std::clamp(t, 0.0f, 1.0f);
    if (t >= 1.0f) {
        value_ = target_;
        animating_ = false;
        return false;
    }
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    value_ = startValue_ + (target_ - startValue_) * eased;
    return true;
}

}  // namespace ffui
