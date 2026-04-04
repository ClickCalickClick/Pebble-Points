# Standard Operating Procedure Guide - Pebble Points
*Guardrails derived from friction.log to prevent recurring incidents.*

---

## Build & Compilation Guardrails

**Math Functions**
- To ensure all math functions compile cleanly, always include `#include <math.h>` before using sin(), cos(), sqrt() or similar functions.

**Standard Library**
- To ensure compatibility across platforms, always use conditional logic instead of relying on MAX/MIN macros (may not exist in all SDK versions).

**Multi-Platform Verification**
- To ensure the app works on all target platforms, always test at least aplite (144×168), basalt (200×228), and chalk (260×260) before marking complete.

**Build Status Validation**
- To ensure rapid error detection, always grep build output for "finished" or "error" rather than reading verbose pebble build logs line-by-line.

**Cache Corruption Prevention**
- To ensure object files reflect current code, always run `pebble clean` before major refactors, especially after header changes.

---

## Animations & Graphics Guardrails

**Opacity Values**
- To ensure animations are reasoning-friendly and standard, always use float values (0.0-1.0) for opacity instead of 0-255 byte ranges.

**Canvas Updates**
- To ensure custom drawing appears, always call `layer_mark_dirty()` after manual canvas updates; note that property animations handle this automatically.

**Organic Motion Feel**
- To ensure animations feel natural rather than mechanical, always use sinusoidal curves for breathing/pulsing: `sin(elapsed * PI / period) * amplitude`.

**Animation Subtlety**
- To ensure animations enhance rather than distract, always keep breathing effects subtle (2px amplitude, 3-5s period); glance-first design requires restrained motion.

**Window Transitions**
- To ensure smooth frame transitions, always set `animated=true` on window stack operations (`window_stack_push()` with `WindowFullscreen`).

**Small Screen Testing**
- To ensure effects are appropriate at all scales, always test animations on aplite (144×168) where breathing should be barely perceptible.

---

## Platform Compatibility Guardrails

**Positioning Strategy**
- To ensure UI adapts to all screen sizes, always use relative positioning (grect_inset(), layer_set_bounds()) instead of hardcoded pixel coordinates.

**Multi-Aspect Testing**
- To ensure layouts work on all platforms, always test on at least 3 different aspect ratios: rectangular (basalt), square-ish (aplite), and circular (chalk).

**Font Sizing**
- To ensure text displays correctly, always add padding margins; text that fits perfectly on basalt may wrap on aplite due to font metrics differences.

**Circular Display Safe Areas**
- To ensure content isn't cut off by circular bezels, always design with safe area centered and test on chalk that truncates beyond radius.

**Memory Constraints**
- To ensure consistent behavior, always assume all platforms have same memory constraints; avoid assuming basalt/emery have extra resources vs aplite.

---

## State Management Guardrails

**Variable Initialization**
- To ensure predictable behavior, always initialize all state variables in `window_load()` rather than relying on global defaults.

**Static Globals Convention**
- To ensure code clarity, always prefix static global variables with `s_` to signal persistence across function calls.

**Animation Cleanup**
- To ensure no memory leaks or crashes on window unload, always call `property_animation_destroy()` and reset timers in `window_unload()`.

**Streak Persistence Logic**
- To ensure timezone-proof streak counting, always use UTC epoch-based milliseconds since 1970 instead of simple date logic.

**Persistent Storage**
- To ensure data integrity, always validate persistent storage before use; outdated data is worse than missing data.

---

## PKJS Integration Guardrails

**Bidirectional Messaging**
- To ensure full communication capability, always design AppMessage endpoints remembering that watch can both send AND receive.

**Dictionary Keys**
- To ensure auto-generated headers match reality, always verify that message keys in appMessage endpoints match **exactly** with package.json dictionary keys.

**Engagement & Personality**
- To ensure better user retention, always add flavor text or "pondering personas" to content; low effort, high impact.

**Daily Content Rotation**
- To ensure variety without server infrastructure, always implement day-of-year modulo rotation for prompts/quotes; provides variety while offline.

---

## Testing Guardrails

**Visual Regression**
- To ensure UI correctness, always take a screenshot after major UI changes; visual diffs catch issues code review often misses.

**Interaction Testing**
- To ensure end-to-end flows work, always use `pebble emu-button` to script button sequences (e.g., SELECT→DOWN→BACK) rather than manual clicking.

**Sleep/Wake State**
- To ensure edge-case bugs surface, always test lock/unlock cycles; some state bugs only appear after screen sleep/wake.

**Headless Environment Testing**
- To ensure CI/headless compatibility, always add `--vnc` flag to all emulator commands (install, screenshot, emu-button) when running without a window server.

**Performance Baseline**
- To ensure animations remain responsive, always animate via opacity floats rather than redrawing complex graphics per frame.

---

## Design Philosophy Guardrails

**Glance-First**
- To ensure usability, always design so information is visible in <2 seconds before user scrolls; primary data in top 1/3.

**Color vs Battery**
- To ensure battery life, always prefer black and white designs over color (adds ~25% power cost for minimal visual benefit).

**Button Functionality**
- To ensure intuitive navigation, always assign ONE primary function per button; multiple actions per button increase cognitive load.

**Ethical Streak Design**
- To ensure sustainable habit-building, always design streaks to encourage healthy routines, not obsessive behavior.

**Breathing Animation Semantics**
- To ensure clear messaging, always use sinusoidal breathing animations to signal "zen mode" (calm/meditative); avoid on high-energy functions.

---

## Double-Mistake Watch
*Mechanism to catch repeated incidents*

If a friction entry matches a learnings guid rule above, escalate as **critical system failure** in friction.log — this signals a guardrail didn't prevent recurrence.

---

**Document Version:** 2026-04-03  
**Last Updated By:** Friction Log Architect  
**Review Cadence:** After each friction.log entry resolution
