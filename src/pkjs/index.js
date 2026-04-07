// PebbleKit JS - Phone-side application for Pebble Points
// Handles settings UI and bidirectional AppMessage communication

// Import the Clay package
var Clay = require('@rebble/clay');
var messageKeys = require('message_keys');
// Load our Clay configuration file
var clayConfig = require('./config.json');
// Initialize Clay with manual event handling
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// Debug logging  
function log(msg) {
  console.log('[Pebble Points] ' + msg);
}

// ============================================================================
// APP MESSAGE HANDLERS
// ============================================================================

// Called when watch is ready to receive messages
Pebble.addEventListener('ready', function(e) {
  log('PebbleKit JS ready');
});

// Open the Clay configuration page
Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

// ============================================================================
// SETTINGS UI & CLAY FRAMEWORK
// ============================================================================

// Listen for webviewclosed to send settings to watch
Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response) {
    log('Configuration closed - Response: ' + e.response);

    var settings = clay.getSettings(e.response);
    log('Converted settings: ' + JSON.stringify(settings));

    // Persist settings for future config opens
    clay.setSettings(settings);

    // Send settings to watch via AppMessage
    sendSettingsToWatch(settings);
  } else {
    log('Configuration cancelled');
  }
});

// ============================================================================
// MESSAGE KEY HANDLERS (AppMessage)
// ============================================================================

// Handle messages FROM the watch
Pebble.addEventListener('appmessage', function(e) {
  log('Message from watch: ' + JSON.stringify(e.payload));
  
  // Handle score updates from watch if needed in future
  if (e.payload.score_update) {
    log('Score update received: ' + e.payload.score_update);
  }
});

// Handle watch disconnection/errors
Pebble.addEventListener('appmessageerror', function(e) {
  log('AppMessage error: ' + e.error);
});

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Send settings to watch
function sendSettingsToWatch(settings) {
  var messageDict = settings || {};

  // Clay returns AppMessage-ready objects keyed by numeric message keys.
  if (typeof messageDict[messageKeys.playerCount] !== 'undefined') {
    messageDict[messageKeys.playerCount] = parseInt(messageDict[messageKeys.playerCount], 10);
  }

  // Normalize toggle payloads to numeric 0/1 values for consistent watch-side parsing.
  if (typeof messageDict[messageKeys.enableHaptics] !== 'undefined') {
    messageDict[messageKeys.enableHaptics] = messageDict[messageKeys.enableHaptics] ? 1 : 0;
  }
  if (typeof messageDict[messageKeys.enableConfetti] !== 'undefined') {
    messageDict[messageKeys.enableConfetti] = messageDict[messageKeys.enableConfetti] ? 1 : 0;
  }
  
  // Send to watch
  Pebble.sendAppMessage(messageDict,
    function() {
      log('Settings sent to watch: ' + JSON.stringify(messageDict));
    },
    function(e) {
      log('Failed to send settings to watch: ' + e.error.message);
    }
  );
}

// Get default settings
function getDefaultSettings() {
  return {
    playerCount: 4,
    playerNames: ['P1', 'P2', 'P3', 'P4'],
    enableConfetti: true,
    enableHaptics: true
  };
}

log('PebbleKit JS loaded');

