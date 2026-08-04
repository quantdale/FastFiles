// Minimal gated animation infrastructure for the modernized UI surfaces.
//
// FastFiles animates scroll offsets and opacities in DIP or [0,1] space; these
// helpers animate scalar targets with an ease-out cubic curve. The owner
// (WindowShell) drives animations from its WM_TIMER handler — FloatAnimation
// never creates timers; it only reports state and advances a frame.
#pragma once

#include <cstdint>

namespace ffui {

// Default fade/slide duration (ms), mid-point of the design's 100-180 ms
// window for UI transitions.
constexpr float kUiAnimationDefaultMs = 150.0f;

// Reports whether the system's "Animation effects" are enabled, via
// SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION). Defaults to true when the
// query fails (older Windows). Used to gate FloatAnimation so reduced-motion
// users get instant state changes.
bool SystemAnimationsEnabled();

// Animates a single scalar (scroll offset, opacity, progress) toward a target
// with ease-out cubic easing. No per-frame allocations. Time is measured in
// milliseconds via a caller-supplied monotonic clock (GetTickCount64) and held
// as uint64_t so the (now - start) difference is exact before easing.
class FloatAnimation {
public:
    explicit FloatAnimation(float value = 0.0f)
        : value_(value), startValue_(value), target_(value), startMs_(0), durationMs_(0.0f), animating_(false) {}

    // Begins (or re-targets) an animation from the current value. When system
    // animations are disabled, or durationMs <= 0, snaps to target instead of
    // animating. nowMs is the caller's current timestamp in ms.
    void AnimateTo(float target, float durationMs, uint64_t nowMs);

    float Value() const { return value_; }
    bool IsAnimating() const { return animating_; }

    // Advances one frame using the caller's latest clock reading (WM_TIMER
    // handler). Returns true while the animation is still in progress, false
    // when it settled (value_ has reached target_ and animating_ is false).
    bool Tick(uint64_t nowMs);

    // Jumps to value without animating.
    void Snap(float value) {
        value_ = value;
        startValue_ = value;
        target_ = value;
        animating_ = false;
    }

private:
    float value_;
    float startValue_;
    float target_;
    uint64_t startMs_;
    float durationMs_;
    bool animating_;
};

}  // namespace ffui
