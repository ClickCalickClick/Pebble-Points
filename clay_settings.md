# Clay Settings Setup Guide (Rebble/Pebble)

Purpose: a reusable recipe for adding Clay app settings support to any Pebble repo.

This guide captures the exact setup that works in this project and the mistakes to avoid.

## 1) Required Package and Manifest Wiring

1. Install Clay:

   npm install @rebble/clay

2. In package.json, set configurable capability using the modern format:

   - Use: "capabilities": ["configurable"]
   - Do not use: "configurable": true

3. Define all settings keys in package.json pebble.messageKeys.

Example shape:

```json
{
  "pebble": {
    "capabilities": ["configurable"],
    "messageKeys": [
      "playerCount",
      "player1Name",
      "player2Name",
      "player3Name",
      "player4Name",
      "enableConfetti",
      "enableHaptics"
    ]
  },
  "dependencies": {
    "@rebble/clay": "^1.0.8"
  }
}
```

## 2) Create src/pkjs/config.json

Create the Clay form config at src/pkjs/config.json.

Rules:
1. Use messageKey values that exactly match package.json messageKeys.
2. Include a submit component at the bottom. Without it, changes will not save.
3. Add input constraints (for example maxlength) in config JSON.

Minimal example:

```json
[
  { "type": "heading", "defaultValue": "App Settings" },
  {
    "type": "toggle",
    "messageKey": "enableHaptics",
    "label": "Enable Vibration Feedback",
    "defaultValue": true
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
]
```

## 3) Create or Update src/pkjs/index.js

Use Clay with explicit manual event handling:

```javascript
var Clay = require('@rebble/clay');
var messageKeys = require('message_keys');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response) {
    return;
  }

  var settings = clay.getSettings(e.response);
  clay.setSettings(settings);

  // Normalize payload types before send.
  if (typeof settings[messageKeys.playerCount] !== 'undefined') {
    settings[messageKeys.playerCount] = parseInt(settings[messageKeys.playerCount], 10);
  }
  if (typeof settings[messageKeys.enableHaptics] !== 'undefined') {
    settings[messageKeys.enableHaptics] = settings[messageKeys.enableHaptics] ? 1 : 0;
  }

  Pebble.sendAppMessage(settings);
});
```

Important:
1. Do not JSON.parse(e.response) directly.
2. Use clay.getSettings(e.response) and clay.setSettings(settings).
3. Clay payload keys are AppMessage-style keys; use message_keys mapping when coercing values.

## 4) Watch-Side C Integration

In C:
1. Include message_keys.auto.h.
2. In inbox_received handler, read tuples with MESSAGE_KEY_* keys.
3. Parse robustly (string or int where needed).
4. Persist state immediately after applying settings.
5. Refresh any cached render buffers after updates.

Skeleton:

```c
#include "message_keys.auto.h"

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
  Tuple *haptics_tuple = dict_find(iterator, MESSAGE_KEY_enableHaptics);
  if (haptics_tuple) {
    if (haptics_tuple->type == TUPLE_CSTRING) {
      const char *raw = haptics_tuple->value->cstring;
      s_game.enable_haptics = (raw && strcmp(raw, "1") == 0) ? 1 : 0;
    } else {
      s_game.enable_haptics = haptics_tuple->value->int32 ? 1 : 0;
    }
  }

  game_session_save(&s_game);
  render_player_scores();
}
```

Haptics guardrail:
1. Route all vibration calls through helper functions that check s_game.enable_haptics.
2. Do not call vibes_* directly from handlers.

## 5) Build and Verify Checklist

1. Build:

   pebble build

2. Install on emulator:

   pebble install --emulator basalt

3. Phone-side behavior check:
1. Open app settings in companion app.
2. Toggle a value and tap Save Settings.
3. Confirm watch receives and applies changes.

4. If headless, add --vnc to emulator-interacting commands.

## 6) Common Failure Modes

1. Missing submit button in config.json.
2. Using pebble-clay instead of @rebble/clay.
3. Using old manifest flag format.
4. Parsing e.response directly instead of clay.getSettings.
5. Not normalizing toggle payload types before AppMessage send.
6. Updating watch state but not refreshing render buffers.
7. Having enableHaptics in settings but leaving direct vibes_* calls unguarded.

## 7) New Repo Quick Start (Copy Sequence)

1. npm install @rebble/clay
2. Add capabilities and messageKeys to package.json
3. Add src/pkjs/config.json with submit button
4. Add manual Clay flow in src/pkjs/index.js
5. Add C inbox handler using message_keys.auto.h
6. Persist + redraw on settings update
7. Build and test settings round trip
