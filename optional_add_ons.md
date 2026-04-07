Great set to explore. These four are all viable, and they fit your current architecture cleanly.

Where they would plug in:

Core input/render/persistence: Pebble Points.c
Settings UI: config.json
PKJS settings bridge: index.js
Message key registry: package.json
Low-Hanging 1: Quick Reset Gesture (long Select resets active player)
How it works for users:

Normal Select still cycles active player.
Long Select on game screen resets only the active player score to zero.
Immediate feedback:
one stronger haptic pulse
delta marker clears so no stale +/-
score redraws instantly
Implementation shape:

Add long-click subscription in the game window button config provider.
Add handler that:
validates active index
sets active player score to 0
clears last-delta state
saves session
redraws layer
Keep this behavior only in gameplay window so menus are unaffected.
Guardrails:

Accidental trigger prevention: long press threshold around 650-800 ms.
No-op behavior if score already 0.
Optional confirm mode later if desired:
first long press arms reset
second within 1.5s confirms
Risk level:

Very low.
No storage schema change required.
Minimal UI regression risk.
Low-Hanging 3: Configurable Score Step (+/-1, +/-5, +/-10)
How it works for users:

Add a setting named Score Step in phone settings.
User picks 1, 5, or 10.
Up/Down increments by the selected amount.
Delta marker reflects true change amount and accumulates correctly.
Implementation shape:

Add Score Step setting in config.json.
Add message key in package.json, then use generated key in C.
In index.js, send selected step to watch.
In Pebble Points.c:
store score_step in session state
validate incoming value against allowed set
replace hardcoded +/-1 with +/-score_step in handlers
Persistence/migration:

If added into persisted struct, include safe default on load.
For legacy saves, fallback to 1 if value absent or invalid.
Risk level:

Low to medium.
Main risk is persistence migration edge cases.
UI risk is low because layout already supports multi-digit values, but should still test negatives with larger steps.
Low-Hanging 5: Battery-Lite Mode
How it works for users:

Toggle in settings: Battery Lite.
When enabled, visuals simplify and redraw cost drops.
App still behaves the same functionally.
What to reduce first:

Disable non-essential accents:
optional hide delta marker
reduce border complexity
lower corner radius to 0 or keep simple fills
Skip optional visual work:
no decorative redraws beyond score/name updates
Keep core readability:
names and scores remain high-contrast
Implementation shape:

Add toggle in config.json and bridge through index.js.
Persist flag in game session state in Pebble Points.c.
Gate drawing branches in quadrant render path on battery-lite flag.
Why it is good:

Easy to ship incrementally.
Gives users control.
Future-proofs if animation features are re-enabled later.
Risk level:

Low.
Biggest risk is readability if over-simplified; mitigate by keeping text contrast unchanged.
Creative 5: Session Timeline View (continue-menu detail)
How it works for users:

Continue menu currently lists saves by timestamp.
Add a per-save detail view that shows recent score progression checkpoints.
User flow:
select a save
choose View Timeline or Load Game
timeline shows newest-to-oldest key events/checkpoints
back returns to save list
What data to store:

Lightweight checkpoint ring buffer per save slot.
Each checkpoint can contain:
timestamp
changed player index
delta
resulting score
Keep fixed-size buffer (for example 8-12 entries) to cap memory.
When to checkpoint:

Every score change is simplest, but can be noisy.
Better balance:
checkpoint on player switch
checkpoint every N taps
checkpoint on large deltas if score-step > 1
UI on small screens:

Single-line compact entries:
time + player + delta + score
Scrollable menu-style list fits existing Pebble patterns.
Optional tiny sparkline later, but text list is safer first.
Implementation shape:

Extend save-slot structure in Pebble Points.c with fixed checkpoint array.
Update save/write path to append checkpoints.
Add timeline window and menu layer reusing existing continue-menu style.
Keep migration path:
if old saves have no timeline, initialize empty list
Risk level:

Medium.
Most sensitive part is struct growth and migration safety.
UX complexity rises slightly, but payoff is high because it adds personality and context.
Recommended build order:

Quick reset gesture
Configurable score step
Battery-lite mode
Session timeline view
That order gives fastest wins first, then one higher-creativity feature after core stability stays intact.

---

## Checkpoint Replay Rollout Plan (Approved UX + Optimization)

Goal: ship checkpoint replay in an additive way, preserve existing gameplay behavior, and let users watch first then continue directly.

### Product Decision

- Replay mode type: checkpoint-only (not full-event) for v1.
- Entry flow: Continue Game -> pick save -> action menu.
- Action menu rows: Continue, Replay, Back.
- Replay state: read-only (no score edits, no checkpoint writes while replaying).
- Replay exit (Option 1 now):
	- When replay reaches the end, user can continue directly into live game.
	- Replay-to-live transition should not require backtracking to menus.

### Release Sequencing

1. Ship Option 1 now.
1. Add Option 2 later for faster interaction.
1. Consider Option 3 as an advanced phase.

### Option Definitions

- Option 1 (ship now): Replay to end, then continue into the selected live game.
- Option 2 (future): Mid-replay quick continue action for faster handoff.
- Option 3 (future advanced): Continue from an intermediate replay checkpoint as a forked game.

### Implementation Scope for Option 1

- Add replay checkpoint storage as bounded ring buffer per saved game slot.
- Trigger checkpoints on:
	- player switch
	- every N scoring taps
	- large deltas when score-step > 1
- Keep replay data aligned with save slot ordering during promote/new-game rotation.
- Add game action menu window between save selection and opening gameplay.
- Add replay window with timer-driven checkpoint playback (200ms per step default).
- At replay completion, allow direct continue into live gameplay.

### Data + Safety Constraints

- Keep retention to last 3 games (existing slot model).
- Use fixed-size checkpoint records and bounded capacity.
- Never allow replay mode to mutate live scores.
- Only entering live Continue resumes checkpoint recording.

### Validation Checklist

- Continue flow unchanged when user chooses Continue.
- Replay flow is read-only and deterministic.
- Replay end can transition directly to live game.
- Save-slot reordering keeps replay data attached to the correct game.
- Existing saves remain compatible and load without reset.

