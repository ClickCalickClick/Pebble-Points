#include <pebble.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

// Auto-generated message keys from config.json
#include "message_keys.auto.h"

// ============================================================================
// PLATFORM CONFIGURATION
// ============================================================================

// B&W platform detection: Aplite (original B&W), Diorite (Pebble 2), Flint (Pebble 2 Duo)
#define IS_BW_PLATFORM (defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_DIORITE) || defined(PBL_PLATFORM_FLINT))

// ============================================================================
// PHASE 1: Data Structures & Persistence Layer
// ============================================================================

// Storage key for persistent data
#define STORAGE_KEY 1

#define MAX_PLAYERS 4
#define MAX_SAVED_GAMES 3
#define MAX_NAME_LEN 16
#define MAX_PLAYER_NAME_LEN 15  // 16 - 1 for null terminator
#define SELECT_LONG_RESET_DURATION_MS 700
#define SCORE_STEP_DEFAULT 1
#define SCORE_STEP_OPTION_COUNT 3
#define REPLAY_STORAGE_KEY_BASE 200
#define REPLAY_TICK_MS 300
#define CHECKPOINT_TAP_INTERVAL 4
#define MAX_CHECKPOINTS_PER_GAME 8
#define REPLAY_FOOTER_HEIGHT 16

typedef enum {
  CHECKPOINT_REASON_START = 0,
  CHECKPOINT_REASON_PLAYER_SWITCH = 1,
  CHECKPOINT_REASON_TAP_INTERVAL = 2,
  CHECKPOINT_REASON_LARGE_DELTA = 3,
} CheckpointReason;

// Single player state
typedef struct {
  char name[MAX_NAME_LEN];
  int score;
} PlayerState;

// Complete game session
typedef struct {
  PlayerState players[MAX_PLAYERS];
  int active_index;
  int player_count;
  uint32_t created_at;
  uint32_t last_modified;
  uint8_t enable_confetti;   // Feature toggle from phone
  uint8_t enable_haptics;    // Feature toggle from phone
  uint8_t padding[2];  // Reserved for future use; ensures consistent struct size
} GameSession;

// Legacy persisted format used before multi-game support (for migration)
typedef struct {
  PlayerState players[MAX_PLAYERS];
  int active_index;
  int player_count;
  uint32_t last_modified;
  uint8_t enable_confetti;
  uint8_t enable_haptics;
  uint8_t padding[6];
} LegacyGameSessionV1;

// Persistent container for up to 3 games ordered newest -> oldest
typedef struct {
  GameSession games[MAX_SAVED_GAMES];
  uint8_t game_count;
  uint8_t active_game_index;
  uint8_t padding[2];
} GameStorage;

typedef struct {
  int32_t scores[MAX_PLAYERS];
  uint8_t player_count;
  uint8_t active_index;
  int8_t changed_player;
  int8_t delta;
  uint8_t reason;
  uint8_t reserved;
} ReplayCheckpoint;

typedef struct {
  ReplayCheckpoint checkpoints[MAX_CHECKPOINTS_PER_GAME];
  uint8_t count;
  uint8_t write_index;
  uint8_t taps_since_checkpoint;
  uint8_t reserved;
} ReplayTrack;

// Global game state (static prefix per SOP_Guide)
static GameSession s_game = {0};
static GameStorage s_store = {0};
static ReplayTrack s_replay_tracks[MAX_SAVED_GAMES] = {0};
static int s_active_game_index = -1;

// Window and layer declarations (needed early for click handlers)
static Window *s_main_menu_window = NULL;
static Window *s_continue_menu_window = NULL;
static Window *s_game_window = NULL;
static Window *s_settings_window = NULL;
static Window *s_game_action_window = NULL;
static Window *s_replay_window = NULL;
static Layer *s_game_layer = NULL;
static Layer *s_replay_layer = NULL;
static MenuLayer *s_main_menu_layer = NULL;
static MenuLayer *s_continue_menu_layer = NULL;
static MenuLayer *s_settings_menu_layer = NULL;
static MenuLayer *s_game_action_menu_layer = NULL;
static TextLayer *s_main_menu_title_layer = NULL;
static TextLayer *s_continue_menu_title_layer = NULL;
static TextLayer *s_settings_title_layer = NULL;
static TextLayer *s_game_action_title_layer = NULL;

static uint8_t s_selected_continue_index = 0;
static uint8_t s_replay_source_index = 0;
static GameSession s_replay_seed_game = {0};
static GameSession s_replay_view_game = {0};
static int s_replay_play_index = 0;
static bool s_replay_mode = false;
static bool s_replay_complete = false;
static bool s_consume_next_game_select = false;
static AppTimer *s_replay_timer = NULL;

static const uint8_t SCORE_STEP_OPTIONS[SCORE_STEP_OPTION_COUNT] = {1, 5, 10};

// Last score-change indicator state (single active marker)
static int s_last_delta_player = -1;
static int s_last_delta_value = 0;

// ============================================================================
// PHASE 2: Dynamic UI Layout System
// ============================================================================

#define HEADER_HEIGHT 20
#define GUTTER_SIZE 4
#define BORDER_RADIUS 6
#define SELECTION_BORDER_WIDTH 3
#define SELECTION_BORDER_INSET ((GEdgeInsets) {2, 2, 2, 2})

#if defined(PBL_ROUND)
  #define ROUND_TITLE_TOP_PADDING 6
  #define ROUND_DELTA_EDGE_INSET_X 12
  #define ROUND_DELTA_EDGE_INSET_Y 4
  #define ROUND_DELTA_CORNER_EXTRA_X 10
  #define ROUND_DELTA_CORNER_EXTRA_Y 4
#else
  #define ROUND_TITLE_TOP_PADDING 0
#endif

// Layout configuration for dynamic grid
typedef struct {
  GRect quadrants[MAX_PLAYERS];  // Bounds for each player's quadrant
  int row_count;                 // 1, 2, or 2 (depends on player count)
  int col_count;                 // Varies by layout
} LayoutConfig;

// Generate layout configuration based on player count
static LayoutConfig layout_get_config(GRect bounds, int player_count) {
  LayoutConfig layout = {0};
  const int replay_footer_reserved = s_replay_mode ? REPLAY_FOOTER_HEIGHT : 0;
  const int header_reserved = HEADER_HEIGHT + ROUND_TITLE_TOP_PADDING;
  
  // Account for header at top and gutters
  GRect usable = GRect(
    bounds.origin.x + GUTTER_SIZE,
    bounds.origin.y + header_reserved + GUTTER_SIZE,
    bounds.size.w - (2 * GUTTER_SIZE),
    bounds.size.h - header_reserved - replay_footer_reserved - (2 * GUTTER_SIZE)
  );
  
  if (player_count == 2) {
    // Vertical split: top/bottom, full width
    layout.row_count = 2;
    layout.col_count = 1;
    
    int half_height = usable.size.h / 2;
    
    // Player 0: top half
    layout.quadrants[0] = GRect(
      usable.origin.x,
      usable.origin.y,
      usable.size.w,
      half_height
    );
    
    // Player 1: bottom half
    layout.quadrants[1] = GRect(
      usable.origin.x,
      usable.origin.y + half_height,
      usable.size.w,
      usable.size.h - half_height
    );
    
  } else if (player_count == 3) {
    // Inverted T: Player 0 top (full width), Players 1&2 bottom (side-by-side)
    layout.row_count = 2;
    layout.col_count = 2;
    
    int top_height = usable.size.h / 2;
    int bottom_height = usable.size.h - top_height;
    int half_width = usable.size.w / 2;
    
    // Player 0: top half, full width
    layout.quadrants[0] = GRect(
      usable.origin.x,
      usable.origin.y,
      usable.size.w,
      top_height
    );
    
    // Player 1: bottom-left
    layout.quadrants[1] = GRect(
      usable.origin.x,
      usable.origin.y + top_height,
      half_width,
      bottom_height
    );
    
    // Player 2: bottom-right
    layout.quadrants[2] = GRect(
      usable.origin.x + half_width,
      usable.origin.y + top_height,
      usable.size.w - half_width,
      bottom_height
    );
    
  } else {
    // 4 players: 2x2 grid
    layout.row_count = 2;
    layout.col_count = 2;
    
    int half_height = usable.size.h / 2;
    int half_width = usable.size.w / 2;
    
    // Player 0: top-left
    layout.quadrants[0] = GRect(
      usable.origin.x,
      usable.origin.y,
      half_width,
      half_height
    );
    
    // Player 1: top-right
    layout.quadrants[1] = GRect(
      usable.origin.x + half_width,
      usable.origin.y,
      usable.size.w - half_width,
      half_height
    );
    
    // Player 2: bottom-left
    layout.quadrants[2] = GRect(
      usable.origin.x,
      usable.origin.y + half_height,
      half_width,
      usable.size.h - half_height
    );
    
    // Player 3: bottom-right
    layout.quadrants[3] = GRect(
      usable.origin.x + half_width,
      usable.origin.y + half_height,
      usable.size.w - half_width,
      usable.size.h - half_height
    );
  }
  
  return layout;
}

static void replay_draw_footer(GContext *ctx, GRect bounds) {
  if (!s_replay_mode) {
    return;
  }

  GRect footer_rect = GRect(
    bounds.origin.x,
    bounds.origin.y + bounds.size.h - REPLAY_FOOTER_HEIGHT,
    bounds.size.w,
    REPLAY_FOOTER_HEIGHT
  );

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, footer_rect, 0, GCornerNone);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(
    ctx,
    s_replay_complete ? "Select: Continue  Up: Replay" : "Select: Skip end  Back: Exit",
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    footer_rect,
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );
}

// Current layout configuration (updated in window_load)
static LayoutConfig s_layout = {0};

// Persistent text buffers (for graphics_draw_text rendering)
static char s_score_text[MAX_PLAYERS][16] = {0};
static char s_name_text[MAX_PLAYERS][MAX_NAME_LEN] = {0};

// ============================================================================
// FORWARD DECLARATIONS (for function dependencies)
// ============================================================================

static GColor player_get_color(int player_index);
static LayoutConfig layout_get_config(GRect bounds, int player_count);
static void render_player_scores(void);
static void game_session_init_defaults(GameSession *session);
static void game_session_save(GameSession *session);
static void replay_apply_checkpoint(const ReplayCheckpoint *checkpoint);

// ============================================================================
// PHASE 3: Animation System Infrastructure (Steps 7-11)
// ============================================================================

// Animation types
typedef enum {
  ANIM_TYPE_POP,
  ANIM_TYPE_SLASH,
  ANIM_TYPE_BORDER,
  ANIM_TYPE_FLOAT_TEXT,
  ANIM_TYPE_CONFETTI,
} AnimationType;

// Context for tracking active animations
typedef struct {
  Animation *current_anim;
  AnimationType type;
  int player_index;
  uint32_t start_time;
} AnimationContext;

// Global animation context
static AnimationContext s_anim_context = {0};

// ============================================================================
// FLOATING TEXT ANIMATION (Step 10)
// ============================================================================

// Floating text layer context for +1 animations
typedef struct {
  Layer *parent;
  TextLayer *floating_text;
  AppTimer *cleanup_timer;
  int initial_y;
  int target_y;
  Animation *movement_anim;
} FloatingTextContext;

// Pool of floating text contexts (max 4 simultaneous floating texts)
#define MAX_FLOATING_TEXTS 4
static FloatingTextContext s_floating_texts[MAX_FLOATING_TEXTS] = {0};

// Helper: Find an available floating text slot
static FloatingTextContext *floating_text_get_available(void) {
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (s_floating_texts[i].floating_text == NULL) {
      return &s_floating_texts[i];
    }
  }
  // All slots full, reuse first one
  return &s_floating_texts[0];
}

// Helper: Clean up a floating text (called via timer)
static void floating_text_cleanup(void *data) {
  FloatingTextContext *ctx = (FloatingTextContext *)data;
  if (!ctx) return;
  
  if (ctx->movement_anim) {
    animation_unschedule(ctx->movement_anim);
    animation_destroy(ctx->movement_anim);
    ctx->movement_anim = NULL;
  }
  
  if (ctx->floating_text) {
    text_layer_destroy(ctx->floating_text);
    ctx->floating_text = NULL;
  }
  
  if (ctx->cleanup_timer) {
    app_timer_cancel(ctx->cleanup_timer);
    ctx->cleanup_timer = NULL;
  }
  
  ctx->parent = NULL;
}

// Helper: Cancel any active animation
static void animation_cancel_active(void) {
  if (s_anim_context.current_anim) {
    animation_unschedule(s_anim_context.current_anim);
    animation_destroy(s_anim_context.current_anim);
    s_anim_context.current_anim = NULL;
  }
  s_anim_context.type = ANIM_TYPE_POP;  // Reset to safe default
  s_anim_context.player_index = 0;
}

// Spawn a floating +1 text below the score
static void floating_text_spawn(Layer *parent, int quadrant_index, GColor color, int center_x, int center_y) {
  FloatingTextContext *ctx = floating_text_get_available();
  
  // Clean up previous content if reusing a slot
  if (ctx->floating_text) {
    floating_text_cleanup(ctx);
  }
  
  ctx->parent = parent;
  // Position below the score (score ends around center_y + 12, add 4px spacing)
  ctx->initial_y = center_y + 16;
  ctx->target_y = center_y - 4;  // Float up 20 pixels from starting position
  
  // Create floating text layer (positioned below score with spacing)
  GRect floating_rect = GRect(center_x - 20, center_y + 16 - 10, 40, 20);
  ctx->floating_text = text_layer_create(floating_rect);
  text_layer_set_text(ctx->floating_text, "+1");
  text_layer_set_font(ctx->floating_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(ctx->floating_text, GTextAlignmentCenter);
  text_layer_set_text_color(ctx->floating_text, color);
  text_layer_set_background_color(ctx->floating_text, GColorClear);
  layer_add_child(parent, text_layer_get_layer(ctx->floating_text));
  
  // Schedule cleanup timer for 500ms
  ctx->cleanup_timer = app_timer_register(500, floating_text_cleanup, ctx);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Floating +1 spawned for quadrant %d at (%d, %d)", 
          quadrant_index, center_x, center_y);
}

// ============================================================================
// CONFETTI ANIMATION (Step 11)
// ============================================================================

typedef struct {
  int x, y;           // Current position
  int vx, vy;         // Velocity
  int life;           // Remaining lifetime in ms
  GColor color;       // Particle color
} Particle;

#define MAX_CONFETTI_PARTICLES 8
typedef struct {
  Particle particles[MAX_CONFETTI_PARTICLES];
  Layer *layer;
  AppTimer *cleanup_timer;
  uint32_t start_time;
  int particle_count;
} ConfettiContext;

static ConfettiContext s_confetti_ctx = {0};

// Confetti particle rendering callback
static void confetti_update_proc(Layer *layer, GContext *ctx) {
  // Skip confetti on B&W platforms (colorful confetti invisible and takes resources)
  #if IS_BW_PLATFORM
    return;
  #endif
  
  if (s_confetti_ctx.particle_count == 0) return;
  
  // Draw particles - simple burst pattern
  for (int i = 0; i < s_confetti_ctx.particle_count; i++) {
    Particle *p = &s_confetti_ctx.particles[i];
    
    if (p->x > 0 && p->x < layer_get_bounds(layer).size.w &&
        p->y > 0 && p->y < layer_get_bounds(layer).size.h) {
      // Draw 2x2 pixel rectangle for each particle
      graphics_context_set_fill_color(ctx, p->color);
      graphics_fill_rect(ctx, GRect(p->x, p->y, 2, 2), 0, GCornerNone);
    }
  }
}

// Spawn confetti burst animation
static void confetti_burst(Layer *parent, int center_x, int center_y, GColor color) {
  // Cancel previous confetti if still active
  if (s_confetti_ctx.cleanup_timer) {
    app_timer_cancel(s_confetti_ctx.cleanup_timer);
  }
  
  s_confetti_ctx.layer = parent;
  s_confetti_ctx.start_time = 0;
  s_confetti_ctx.particle_count = MAX_CONFETTI_PARTICLES;
  
  // Create particles in a burst pattern (8 directions)
  const int speeds[8] = {150, 130, 150, 130, 150, 130, 150, 130};  // Alternating speeds
  
  for (int i = 0; i < MAX_CONFETTI_PARTICLES; i++) {
    Particle *p = &s_confetti_ctx.particles[i];
    
    // Calculate velocity based on octant direction
    // Angles: 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°
    int angle_idx = i % 8;
    int speed = speeds[i];
    
    // Pre-calculated approximate cos/sin for 8 directions
    static const int cos_vals[8] = {100, 70, 0, -70, -100, -70, 0, 70};
    static const int sin_vals[8] = {0, 70, 100, 70, 0, -70, -100, -70};
    
    p->x = center_x;
    p->y = center_y;
    p->vx = (cos_vals[angle_idx] * speed) / 100;  // Normalize
    p->vy = (sin_vals[angle_idx] * speed) / 100;
    p->life = 500;  // 500ms lifetime
    p->color = color;
  }
  
  // Schedule cleanup for 500ms
  s_confetti_ctx.cleanup_timer = app_timer_register(500, floating_text_cleanup, NULL);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Confetti burst at (%d, %d) with color", center_x, center_y);
}

// ============================================================================
// ANIMATION IMPLEMENTATIONS (Step 8)
// ============================================================================

// Pop animation: Scale score layer up and back down (300ms, EaseOut)
static void pop_animation_setup(Animation *animation) {
  // Setup - no special state needed
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Pop animation starting for player %d", 
          s_anim_context.player_index);
}

static void pop_animation_update(Animation *animation, const AnimationProgress progress) {
  // progress: 0 to 65535 (ANIMATION_NORMALIZED_MAX)
  // Create scaling effect: 1.0 → 1.2 → 1.0 (ease out)
  // For now, just log progress; actual scaling will be in Phase 3b
  
  if (progress == ANIMATION_NORMALIZED_MAX) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Pop animation complete");
  }
}

static void pop_animation_teardown(Animation *animation) {
  // Cleanup after pop
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Pop animation teardown");
  animation_cancel_active();
  
  // DISABLED: Floating +1 and confetti animations (commented out, infrastructure preserved)
  /*
  // Spawn floating +1 animation (Step 10)
  if (s_game.active_index < s_game.player_count && s_game_layer) {
    LayoutConfig layout = layout_get_config(layer_get_bounds(s_game_layer), s_game.player_count);
    GRect quad = layout.quadrants[s_game.active_index];
    int center_x = quad.origin.x + (quad.size.w / 2);
    int center_y = quad.origin.y + (quad.size.h / 2);
    
    floating_text_spawn(s_game_layer, s_game.active_index, 
                       player_get_color(s_game.active_index), center_x, center_y);
    
    // Trigger confetti burst every 10 points (Step 11)
    int score = s_game.players[s_game.active_index].score;
    if (score > 0 && score % 10 == 0) {
      confetti_burst(s_game_layer, center_x, center_y, 
                    player_get_color(s_game.active_index));
    }
  }
  */
}

static const AnimationImplementation pop_animation_impl = {
  .setup = pop_animation_setup,
  .update = pop_animation_update,
  .teardown = pop_animation_teardown,
};

// Slash animation: Draw diagonal line across score (200ms)
static void slash_animation_setup(Animation *animation) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Slash animation starting for player %d", 
          s_anim_context.player_index);
}

static void slash_animation_update(Animation *animation, const AnimationProgress progress) {
  // progress: 0 to 65535
  // Flash background or draw line effect
  if (progress == ANIMATION_NORMALIZED_MAX) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Slash animation complete");
  }
}

static void slash_animation_teardown(Animation *animation) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Slash animation teardown");
  animation_cancel_active();
}

static const AnimationImplementation slash_animation_impl = {
  .setup = slash_animation_setup,
  .update = slash_animation_update,
  .teardown = slash_animation_teardown,
};

// Selection border animation: Smooth slide to active player (250ms)
static void border_animation_setup(Animation *animation) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Border animation starting");
}

static void border_animation_update(Animation *animation, const AnimationProgress progress) {
  // Interpolate border position between old and new active player
  // Mark game_layer dirty to trigger redraw
  if (s_game_layer) {
    layer_mark_dirty(s_game_layer);
  }
}

static void border_animation_teardown(Animation *animation) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Border animation teardown");
  animation_cancel_active();
  if (s_game_layer) {
    layer_mark_dirty(s_game_layer);
  }
}

static const AnimationImplementation border_animation_impl = {
  .setup = border_animation_setup,
  .update = border_animation_update,
  .teardown = border_animation_teardown,
};

// ============================================================================
// ANIMATION SCHEDULING (Step 9)
// ============================================================================

// Helper: Schedule a pop animation for score increment
static void animation_schedule_pop(int player_index) {
  animation_cancel_active();  // Stop any previous animation
  
  s_anim_context.player_index = player_index;
  s_anim_context.type = ANIM_TYPE_POP;
  
  Animation *anim = animation_create();
  animation_set_duration(anim, 300);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &pop_animation_impl);
  animation_schedule(anim);
  
  s_anim_context.current_anim = anim;
}

// Helper: Schedule a slash animation for score decrement
static void animation_schedule_slash(int player_index) {
  animation_cancel_active();
  
  s_anim_context.player_index = player_index;
  s_anim_context.type = ANIM_TYPE_SLASH;
  
  Animation *anim = animation_create();
  animation_set_duration(anim, 200);
  animation_set_curve(anim, AnimationCurveLinear);
  animation_set_implementation(anim, &slash_animation_impl);
  animation_schedule(anim);
  
  s_anim_context.current_anim = anim;
}

// Helper: Schedule border animation for selection change
static void animation_schedule_border(void) {
  animation_cancel_active();
  
  s_anim_context.type = ANIM_TYPE_BORDER;
  
  Animation *anim = animation_create();
  animation_set_duration(anim, 250);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, &border_animation_impl);
  animation_schedule(anim);
  
  s_anim_context.current_anim = anim;
}

// ============================================================================
// RENDERING (Step 5)
// ============================================================================

// Helper function to get player color for current platform
static GColor player_get_color(int player_index) {
  // Map player index to color (uses only what Pebble SDK supports)
  #if defined(PBL_COLOR) && !IS_BW_PLATFORM
    // Color platforms: Basalt, Chalk, Emery, Gabbro
    switch (player_index % 4) {
      case 0: return GColorCobaltBlue;
      case 1: return GColorYellow;
      case 2: return GColorVividViolet;
      case 3: return GColorOrange;
      default: return GColorWhite;
    }
  #else
    // B&W platforms: Aplite, Diorite (Pebble 2), Flint (Pebble 2 Duo)
    return GColorWhite;
  #endif
}

// Helper: Get text color for high contrast against background
static GColor text_color_for_background(GColor bg_color) {
  #if IS_BW_PLATFORM
    // B&W platforms: White background → Black text, Black background → White text
    if (gcolor_equal(bg_color, GColorWhite)) {
      return GColorBlack;
    }
    return GColorWhite;
  #elif defined(PBL_COLOR)
    // Dark backgrounds (Blue, Purple) → White text
    // Light backgrounds (Yellow, Orange) → Black text
    if (gcolor_equal(bg_color, GColorCobaltBlue) || 
        gcolor_equal(bg_color, GColorVividViolet)) {
      return GColorWhite;
    }
    // Default: assume light background
    return GColorBlack;
  #else
    return GColorWhite;
  #endif
}

static void score_delta_clear(void) {
  s_last_delta_player = -1;
  s_last_delta_value = 0;
}

static void score_delta_track(int player_index, int delta) {
  if (delta == 0) {
    return;
  }

  if (s_last_delta_player == player_index) {
    // Accumulate only while direction remains the same for this player.
    if ((s_last_delta_value > 0 && delta > 0) || (s_last_delta_value < 0 && delta < 0)) {
      s_last_delta_value += delta;
      return;
    }
  }

  // New player or direction flip: start a new signed count.
  s_last_delta_player = player_index;
  s_last_delta_value = delta;
}

static bool score_step_is_valid(int step) {
  for (int i = 0; i < SCORE_STEP_OPTION_COUNT; i++) {
    if (step == SCORE_STEP_OPTIONS[i]) {
      return true;
    }
  }
  return false;
}

static int score_step_normalize(int step) {
  return score_step_is_valid(step) ? step : SCORE_STEP_DEFAULT;
}

static int score_step_get(void) {
  return score_step_normalize((int)s_store.padding[0]);
}

static void score_step_set(int step) {
  s_store.padding[0] = (uint8_t)score_step_normalize(step);
  s_store.padding[1] = 0;
}

static const GameSession *display_session_get(void) {
  return s_replay_mode ? &s_replay_view_game : &s_game;
}

static Layer *display_layer_get(void) {
  return s_replay_mode ? s_replay_layer : s_game_layer;
}

static uint32_t replay_storage_key_for_slot(uint8_t slot_index) {
  return (uint32_t)(REPLAY_STORAGE_KEY_BASE + slot_index);
}

static void replay_track_clear(ReplayTrack *track) {
  if (!track) {
    return;
  }
  memset(track, 0, sizeof(ReplayTrack));
}

static void replay_track_save_slot(uint8_t slot_index) {
  if (slot_index >= MAX_SAVED_GAMES) {
    return;
  }
  persist_write_data(replay_storage_key_for_slot(slot_index),
                     &s_replay_tracks[slot_index], sizeof(ReplayTrack));
}

static void replay_tracks_save_all(void) {
  for (uint8_t i = 0; i < MAX_SAVED_GAMES; i++) {
    replay_track_save_slot(i);
  }
}

static void replay_tracks_load(void) {
  for (uint8_t i = 0; i < MAX_SAVED_GAMES; i++) {
    replay_track_clear(&s_replay_tracks[i]);

    status_t ret = persist_read_data(replay_storage_key_for_slot(i),
                                     &s_replay_tracks[i], sizeof(ReplayTrack));
    if (ret != sizeof(ReplayTrack)) {
      replay_track_clear(&s_replay_tracks[i]);
      continue;
    }

    if (s_replay_tracks[i].count > MAX_CHECKPOINTS_PER_GAME) {
      replay_track_clear(&s_replay_tracks[i]);
      continue;
    }

    if (s_replay_tracks[i].write_index >= MAX_CHECKPOINTS_PER_GAME) {
      s_replay_tracks[i].write_index = 0;
    }
  }
}

static uint8_t replay_track_oldest_index(const ReplayTrack *track) {
  if (!track || track->count == 0) {
    return 0;
  }
  if (track->count < MAX_CHECKPOINTS_PER_GAME) {
    return 0;
  }
  return track->write_index;
}

static const ReplayCheckpoint *replay_track_get_ordered(const ReplayTrack *track,
                                                        uint8_t ordered_index) {
  if (!track || ordered_index >= track->count) {
    return NULL;
  }

  uint8_t oldest = replay_track_oldest_index(track);
  uint8_t physical = (uint8_t)((oldest + ordered_index) % MAX_CHECKPOINTS_PER_GAME);
  return &track->checkpoints[physical];
}

static void replay_track_append_snapshot(uint8_t slot_index, int changed_player,
                                         int delta, CheckpointReason reason) {
  if (slot_index >= MAX_SAVED_GAMES || slot_index >= s_store.game_count) {
    return;
  }

  GameSession *session = &s_store.games[slot_index];
  ReplayTrack *track = &s_replay_tracks[slot_index];

  ReplayCheckpoint checkpoint = {0};
  for (int i = 0; i < MAX_PLAYERS; i++) {
    checkpoint.scores[i] = session->players[i].score;
  }
  checkpoint.player_count = (uint8_t)session->player_count;
  checkpoint.active_index = (uint8_t)session->active_index;
  checkpoint.changed_player = (int8_t)changed_player;
  checkpoint.delta = (int8_t)delta;
  checkpoint.reason = (uint8_t)reason;

  track->checkpoints[track->write_index] = checkpoint;
  if (track->count < MAX_CHECKPOINTS_PER_GAME) {
    track->count++;
  }
  track->write_index = (uint8_t)((track->write_index + 1) % MAX_CHECKPOINTS_PER_GAME);
  track->taps_since_checkpoint = 0;

  replay_track_save_slot(slot_index);
}

static void replay_track_seed_from_game(uint8_t slot_index) {
  if (slot_index >= s_store.game_count || slot_index >= MAX_SAVED_GAMES) {
    return;
  }

  replay_track_clear(&s_replay_tracks[slot_index]);
  replay_track_append_snapshot(slot_index, -1, 0, CHECKPOINT_REASON_START);
}

static void replay_tracks_sync_with_store(void) {
  for (uint8_t i = 0; i < s_store.game_count && i < MAX_SAVED_GAMES; i++) {
    if (s_replay_tracks[i].count == 0) {
      replay_track_seed_from_game(i);
    }
  }

  for (uint8_t i = s_store.game_count; i < MAX_SAVED_GAMES; i++) {
    replay_track_clear(&s_replay_tracks[i]);
    replay_track_save_slot(i);
  }
}

static void replay_track_record_player_switch(void) {
  if (s_active_game_index < 0 || s_active_game_index >= s_store.game_count) {
    return;
  }

  replay_track_append_snapshot((uint8_t)s_active_game_index, s_game.active_index, 0,
                               CHECKPOINT_REASON_PLAYER_SWITCH);
}

static void replay_track_record_score_change(int changed_player, int delta) {
  if (s_active_game_index < 0 || s_active_game_index >= s_store.game_count) {
    return;
  }

  ReplayTrack *track = &s_replay_tracks[s_active_game_index];
  track->taps_since_checkpoint++;

  bool should_checkpoint = false;
  CheckpointReason reason = CHECKPOINT_REASON_TAP_INTERVAL;

  if (abs(delta) > 1) {
    should_checkpoint = true;
    reason = CHECKPOINT_REASON_LARGE_DELTA;
  }

  if (track->taps_since_checkpoint >= CHECKPOINT_TAP_INTERVAL) {
    should_checkpoint = true;
    if (abs(delta) <= 1) {
      reason = CHECKPOINT_REASON_TAP_INTERVAL;
    }
  }

  if (!should_checkpoint) {
    return;
  }

  replay_track_append_snapshot((uint8_t)s_active_game_index, changed_player, delta, reason);
}

static void replay_cancel_timer(void) {
  if (s_replay_timer) {
    app_timer_cancel(s_replay_timer);
    s_replay_timer = NULL;
  }
}

static void replay_sync_view_to_current_game(void) {
  if (s_replay_source_index >= s_store.game_count ||
      s_replay_source_index >= MAX_SAVED_GAMES) {
    return;
  }

  // Always finish replay on the current saved game state, even when the
  // most recent score taps did not trigger a new checkpoint.
  s_replay_view_game = s_store.games[s_replay_source_index];
}

static void replay_set_complete(void) {
  if (!s_replay_complete) {
    s_replay_complete = true;
    replay_sync_view_to_current_game();
    score_delta_clear();
    render_player_scores();
  }
}

static void replay_finish_playback_now(void) {
  if (s_replay_source_index >= MAX_SAVED_GAMES) {
    replay_set_complete();
    return;
  }

  ReplayTrack *track = &s_replay_tracks[s_replay_source_index];
  replay_cancel_timer();

  if (track->count == 0) {
    s_replay_view_game = s_replay_seed_game;
    replay_set_complete();
    return;
  }

  s_replay_play_index = track->count - 1;
  const ReplayCheckpoint *last = replay_track_get_ordered(track, (uint8_t)s_replay_play_index);
  replay_apply_checkpoint(last);
  replay_set_complete();
}

static void replay_apply_checkpoint(const ReplayCheckpoint *checkpoint) {
  if (!checkpoint) {
    return;
  }

  s_replay_view_game = s_replay_seed_game;
  s_replay_view_game.player_count = checkpoint->player_count;
  s_replay_view_game.active_index = checkpoint->active_index;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    s_replay_view_game.players[i].score = checkpoint->scores[i];
  }

  if (checkpoint->changed_player >= 0 && checkpoint->changed_player < MAX_PLAYERS &&
      checkpoint->delta != 0) {
    s_last_delta_player = checkpoint->changed_player;
    s_last_delta_value = checkpoint->delta;
  } else {
    score_delta_clear();
  }

  render_player_scores();
}

static void replay_timer_callback(void *data) {
  (void)data;
  s_replay_timer = NULL;

  if (!s_replay_mode || s_replay_source_index >= MAX_SAVED_GAMES) {
    return;
  }

  ReplayTrack *track = &s_replay_tracks[s_replay_source_index];
  if (track->count == 0 || s_replay_complete) {
    return;
  }

  s_replay_play_index++;
  if (s_replay_play_index >= track->count) {
    replay_set_complete();
    return;
  }

  const ReplayCheckpoint *checkpoint = replay_track_get_ordered(track, (uint8_t)s_replay_play_index);
  replay_apply_checkpoint(checkpoint);

  if (s_replay_play_index + 1 < track->count) {
    s_replay_timer = app_timer_register(REPLAY_TICK_MS, replay_timer_callback, NULL);
  } else {
    replay_set_complete();
  }
}

static void replay_begin_playback(void) {
  replay_cancel_timer();
  score_delta_clear();
  s_replay_play_index = 0;
  s_replay_complete = false;

  if (s_replay_source_index >= MAX_SAVED_GAMES) {
    s_replay_complete = true;
    return;
  }

  ReplayTrack *track = &s_replay_tracks[s_replay_source_index];
  if (track->count == 0) {
    s_replay_view_game = s_replay_seed_game;
    replay_set_complete();
    render_player_scores();
    return;
  }

  const ReplayCheckpoint *first = replay_track_get_ordered(track, 0);
  replay_apply_checkpoint(first);

  if (track->count <= 1) {
    replay_set_complete();
  } else {
    s_replay_timer = app_timer_register(REPLAY_TICK_MS, replay_timer_callback, NULL);
  }
}

// Helper: Draw a colored rounded quadrant with text
static void draw_quadrant(GContext *ctx, GRect quad_bounds, int player_idx,
                         GColor player_color, bool is_selected,
                         const GameSession *session) {
  (void)session;

  // B&W platforms: Invert colors for selected player (black bg, white text)
  // Color platforms: Use standard colored background
  #if IS_BW_PLATFORM
    GColor bg_color = (is_selected) ? GColorBlack : GColorWhite;
    GColor text_color = (is_selected) ? GColorWhite : GColorBlack;
  #else
    GColor bg_color = player_color;
    GColor text_color = text_color_for_background(player_color);
  #endif
  
  // Draw filled rounded rectangle (background)
  graphics_context_set_fill_color(ctx, bg_color);
  graphics_fill_rect(ctx, quad_bounds, BORDER_RADIUS, GCornersAll);
  
  // B&W mode: Add black border to distinguish players (only if not selected to avoid double border)
  #if IS_BW_PLATFORM
    if (!is_selected) {
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_rect(ctx, quad_bounds);
    }
  #endif
  
  // Draw selection border if active (only on color platforms)
  if (is_selected) {
    #if !IS_BW_PLATFORM
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, SELECTION_BORDER_WIDTH);
      GRect inner = grect_inset(quad_bounds, SELECTION_BORDER_INSET);
      graphics_draw_rect(ctx, inner);
    #endif
  }
  
  // Set text color for drawing
  graphics_context_set_text_color(ctx, text_color);
  
  // Draw player name (top of quadrant) - same position for both B&W and color
  GRect name_rect = GRect(
    quad_bounds.origin.x + 4,
    quad_bounds.origin.y + 4,
    quad_bounds.size.w - 8,
    18
  );
  graphics_draw_text(ctx, s_name_text[player_idx], 
                    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                    name_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  
  // Draw player score (center of quadrant) - use more standard font
  GRect score_rect = GRect(
    quad_bounds.origin.x + 2,
    quad_bounds.origin.y + (quad_bounds.size.h / 2) - 16,
    quad_bounds.size.w - 4,
    32
  );
  graphics_draw_text(ctx, s_score_text[player_idx],
                    fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                    score_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Draw last-change indicator only for the most recently changed player.
  if (player_idx == s_last_delta_player && s_last_delta_value != 0) {
    char delta_text[8] = {0};
    snprintf(delta_text, sizeof(delta_text), "%+d", s_last_delta_value);

    // Keep the delta marker near the bottom corner of each quadrant.
    const int delta_w = 34;
    const int delta_h = 20;
    const int inset_x = 2;
    const int inset_y = 2;
    int x = quad_bounds.origin.x + quad_bounds.size.w - delta_w - inset_x;
    int y = quad_bounds.origin.y + quad_bounds.size.h - delta_h - inset_y;
    GTextAlignment delta_align = GTextAlignmentRight;

    #if defined(PBL_ROUND)
      bool round_player4_bottom_left = (player_idx == 3);
      if (round_player4_bottom_left) {
        // Round devices: keep P4 delta away from the lower-right curve.
        x = quad_bounds.origin.x + inset_x + 2;
        delta_align = GTextAlignmentLeft;
      }

      Layer *target_layer = display_layer_get();
      if (target_layer) {
        GRect game_bounds = layer_get_bounds(target_layer);
        int mid_x = game_bounds.origin.x + (game_bounds.size.w / 2);
        int mid_y = game_bounds.origin.y + (game_bounds.size.h / 2);
        bool touches_right = (quad_bounds.origin.x >= mid_x);
        bool touches_bottom = (quad_bounds.origin.y >= mid_y);

        // Pull the marker inward for quadrants that sit on the right/bottom arcs.
        if (touches_right && !round_player4_bottom_left) {
          x -= ROUND_DELTA_EDGE_INSET_X;
        }
        if (touches_bottom) {
          y -= ROUND_DELTA_EDGE_INSET_Y;
        }

        // Corner quadrants need extra padding to stay inside the round mask.
        if (touches_right && touches_bottom && !round_player4_bottom_left) {
          x -= ROUND_DELTA_CORNER_EXTRA_X;
          y -= ROUND_DELTA_CORNER_EXTRA_Y;
        }
      }
    #endif

    GRect delta_rect = GRect(x, y, delta_w, delta_h);
    graphics_draw_text(ctx, delta_text,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                      delta_rect, GTextOverflowModeTrailingEllipsis, delta_align, NULL);
  }
}

// Update all player display text buffers
static void render_player_scores(void) {
  const GameSession *session = display_session_get();

  for (int i = 0; i < session->player_count && i < MAX_PLAYERS; i++) {
    // Update persistent text buffers
    snprintf(s_score_text[i], sizeof(s_score_text[i]), "%d", session->players[i].score);
    snprintf(s_name_text[i], sizeof(s_name_text[i]), "%s", session->players[i].name);
  }
  
  // Mark game layer dirty to trigger redraw
  Layer *target_layer = display_layer_get();
  if (target_layer) {
    layer_mark_dirty(target_layer);
  }
}

// Main rendering function for game layer
static void prv_game_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const GameSession *session = display_session_get();
  
  // Draw black background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
  // Draw "Pebble Points" header
  GRect header_rect = GRect(
    bounds.origin.x,
    bounds.origin.y + ROUND_TITLE_TOP_PADDING,
    bounds.size.w,
    HEADER_HEIGHT
  );
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_replay_mode ? "Replay Mode" : "Pebble Points",
                    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                    header_rect, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  
  // Get current layout based on platform (rectangular vs round)
  LayoutConfig layout = layout_get_config(bounds, session->player_count);
  
  #if defined(PBL_ROUND)
    // ROUND DISPLAY (Chalk, Gabbro): Pie-slice quadrants
    // For now, use rectangular quadrants fitted to circle (Phase 2 enhancement)
    for (int i = 0; i < session->player_count && i < MAX_PLAYERS; i++) {
      bool is_active = (i == session->active_index);
      draw_quadrant(ctx, layout.quadrants[i], i, player_get_color(i), is_active, session);
    }
  #else
    // RECTANGULAR DISPLAY (Basalt, Aplite, etc.): Colored rounded boxes in grid
    for (int i = 0; i < session->player_count && i < MAX_PLAYERS; i++) {
      bool is_active = (i == session->active_index);
      draw_quadrant(ctx, layout.quadrants[i], i, player_get_color(i), is_active, session);
    }
  #endif
  
  // Draw confetti particles (on top of everything)
  confetti_update_proc(layer, ctx);

  // Replay mode footer explains controls and transition behavior.
  replay_draw_footer(ctx, bounds);
}

// Initialize game session with defaults
static void game_session_init_defaults(GameSession *session) {
  // Set 4 default players
  snprintf(session->players[0].name, MAX_NAME_LEN, "P1");
  snprintf(session->players[1].name, MAX_NAME_LEN, "P2");
  snprintf(session->players[2].name, MAX_NAME_LEN, "P3");
  snprintf(session->players[3].name, MAX_NAME_LEN, "P4");

  for (int i = 0; i < MAX_PLAYERS; i++) {
    session->players[i].score = 0;
  }

  session->active_index = 0;
  session->player_count = 4;
  session->created_at = (uint32_t)time(NULL);
  session->last_modified = session->created_at;
  
  // Feature toggles: enabled by default
  session->enable_confetti = 1;
  session->enable_haptics = 1;
  
  memset(session->padding, 0, sizeof(session->padding));
}

// Reset persisted store state
static void game_store_reset(void) {
  memset(&s_store, 0, sizeof(s_store));
  s_store.game_count = 0;
  s_store.active_game_index = 0;
  score_step_set(SCORE_STEP_DEFAULT);
  for (uint8_t i = 0; i < MAX_SAVED_GAMES; i++) {
    replay_track_clear(&s_replay_tracks[i]);
  }
  s_active_game_index = -1;
}

static void game_store_save(void) {
  persist_write_data(STORAGE_KEY, &s_store, sizeof(GameStorage));
}

// Load store from persistence; migrates legacy single-session data when present
static void game_store_load(void) {
  replay_tracks_load();

  status_t ret = persist_read_data(STORAGE_KEY, &s_store, sizeof(GameStorage));
  if (ret == sizeof(GameStorage) && s_store.game_count <= MAX_SAVED_GAMES) {
    score_step_set(score_step_get());

    if (s_store.game_count == 0) {
      s_active_game_index = -1;
      replay_tracks_sync_with_store();
      game_store_save();
      return;
    }

    if (s_store.active_game_index >= s_store.game_count) {
      s_store.active_game_index = 0;
    }

    s_active_game_index = s_store.active_game_index;
    s_game = s_store.games[s_active_game_index];
    replay_tracks_sync_with_store();
    return;
  }

  // Attempt migration from legacy single-session format.
  LegacyGameSessionV1 legacy = {0};
  ret = persist_read_data(STORAGE_KEY, &legacy, sizeof(LegacyGameSessionV1));

  game_store_reset();

  if (ret == sizeof(LegacyGameSessionV1)) {
    GameSession migrated = {0};
    memcpy(migrated.players, legacy.players, sizeof(legacy.players));
    migrated.active_index = legacy.active_index;
    migrated.player_count = legacy.player_count;
    migrated.last_modified = legacy.last_modified;
    migrated.created_at = legacy.last_modified ? legacy.last_modified : (uint32_t)time(NULL);
    migrated.enable_confetti = legacy.enable_confetti;
    migrated.enable_haptics = legacy.enable_haptics;
    memset(migrated.padding, 0, sizeof(migrated.padding));

    if (migrated.player_count < 2 || migrated.player_count > MAX_PLAYERS) {
      game_session_init_defaults(&migrated);
    }

    s_store.games[0] = migrated;
    s_store.game_count = 1;
    s_store.active_game_index = 0;
    s_active_game_index = 0;
    s_game = migrated;
    game_store_save();
  }

  replay_tracks_sync_with_store();
}

static void game_store_promote_to_top(uint8_t index) {
  if (index >= s_store.game_count || index == 0) {
    return;
  }

  GameSession selected = s_store.games[index];
  ReplayTrack selected_track = s_replay_tracks[index];
  for (int i = index; i > 0; i--) {
    s_store.games[i] = s_store.games[i - 1];
    s_replay_tracks[i] = s_replay_tracks[i - 1];
  }
  s_store.games[0] = selected;
  s_replay_tracks[0] = selected_track;
}

static void game_store_ensure_active(void) {
  if (s_store.game_count == 0) {
    GameSession new_game = {0};
    game_session_init_defaults(&new_game);
    s_store.games[0] = new_game;
    s_store.game_count = 1;
    s_store.active_game_index = 0;
    s_active_game_index = 0;
    s_game = new_game;
    replay_track_seed_from_game(0);
    game_store_save();
    return;
  }

  if (s_active_game_index < 0 || s_active_game_index >= s_store.game_count) {
    s_active_game_index = 0;
  }

  s_store.active_game_index = (uint8_t)s_active_game_index;
  s_game = s_store.games[s_active_game_index];
}

static void game_store_start_new_game(void) {
  GameSession new_game = {0};
  game_session_init_defaults(&new_game);
  score_delta_clear();

  int shift_limit = s_store.game_count;
  if (shift_limit > (MAX_SAVED_GAMES - 1)) {
    shift_limit = MAX_SAVED_GAMES - 1;
  }

  for (int i = shift_limit; i > 0; i--) {
    s_store.games[i] = s_store.games[i - 1];
    s_replay_tracks[i] = s_replay_tracks[i - 1];
  }

  s_store.games[0] = new_game;
  if (s_store.game_count < MAX_SAVED_GAMES) {
    s_store.game_count++;
  }

  s_store.active_game_index = 0;
  s_active_game_index = 0;
  s_game = new_game;
  replay_track_seed_from_game(0);
  replay_tracks_save_all();
  game_store_save();
}

static void game_store_continue_game(uint8_t index) {
  if (index >= s_store.game_count) {
    return;
  }

  score_delta_clear();

  game_store_promote_to_top(index);
  s_store.games[0].last_modified = (uint32_t)time(NULL);
  s_store.active_game_index = 0;
  s_active_game_index = 0;
  s_game = s_store.games[0];

  if (s_replay_tracks[0].count == 0) {
    replay_track_seed_from_game(0);
  }

  replay_tracks_save_all();
  game_store_save();
}

// Save current game session into active slot
static void game_session_save(GameSession *session) {
  if (s_store.game_count == 0) {
    s_store.game_count = 1;
    s_active_game_index = 0;
    s_store.active_game_index = 0;
  }

  if (s_active_game_index < 0 || s_active_game_index >= s_store.game_count) {
    s_active_game_index = 0;
  }

  session->last_modified = (uint32_t)time(NULL);
  if (session->created_at == 0) {
    session->created_at = session->last_modified;
  }

  s_store.games[s_active_game_index] = *session;
  s_store.active_game_index = (uint8_t)s_active_game_index;
  game_store_save();
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_consume_next_game_select) {
    // Prevent replay-exit select from mutating gameplay state.
    s_consume_next_game_select = false;
    return;
  }

  // Haptic feedback (Step 14)
  vibes_long_pulse();

  // Clear previous player's delta marker when switching players.
  score_delta_clear();
  
  // Cycle to next player
  s_game.active_index = (s_game.active_index + 1) % s_game.player_count;
  game_session_save(&s_game);
  replay_track_record_player_switch();
  
  // Trigger selection border animation
  animation_schedule_border();
  
  // Update display
  render_player_scores();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "SELECT: Active player now %d", s_game.active_index);
}

static void prv_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_game.active_index < 0 || s_game.active_index >= s_game.player_count ||
      s_game.active_index >= MAX_PLAYERS) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "SELECT long: invalid active index %d", s_game.active_index);
    return;
  }

  if (s_game.players[s_game.active_index].score == 0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "SELECT long: no-op, player %d already at 0", s_game.active_index);
    return;
  }

  s_game.players[s_game.active_index].score = 0;
  score_delta_clear();
  game_session_save(&s_game);
  vibes_double_pulse();
  render_player_scores();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "SELECT long: reset player %d to 0", s_game.active_index);
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  const int score_step = score_step_get();

  // Haptic feedback (Step 14)
  vibes_short_pulse();
  
  // Increment active player score
  score_delta_track(s_game.active_index, score_step);
  s_game.players[s_game.active_index].score += score_step;
  game_session_save(&s_game);
  replay_track_record_score_change(s_game.active_index, score_step);
  
  // Trigger pop animation
  animation_schedule_pop(s_game.active_index);
  
  // Update display
  render_player_scores();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "UP: Player %d score now %d (step=%d)", 
          s_game.active_index, s_game.players[s_game.active_index].score, score_step);
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  const int score_step = score_step_get();

  // Haptic feedback (Step 14)
  vibes_short_pulse();
  
  // Decrement active player score (negative scores are allowed)
  score_delta_track(s_game.active_index, -score_step);
  s_game.players[s_game.active_index].score -= score_step;
  game_session_save(&s_game);
  replay_track_record_score_change(s_game.active_index, -score_step);
  
  // Trigger slash animation
  animation_schedule_slash(s_game.active_index);
  
  // Update display
  render_player_scores();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "DOWN: Player %d score now %d (step=%d)", 
          s_game.active_index, s_game.players[s_game.active_index].score, score_step);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, SELECT_LONG_RESET_DURATION_MS,
                              prv_select_long_click_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler);
}

// ============================================================================
// WINDOW HANDLERS (Step 3)
// ============================================================================

static void format_timestamp_short(uint32_t timestamp, char *buffer, size_t buffer_len) {
  if (timestamp == 0) {
    snprintf(buffer, buffer_len, "--/-- --:--");
    return;
  }

  time_t t = (time_t)timestamp;
  struct tm *tm_info = localtime(&t);
  if (!tm_info) {
    snprintf(buffer, buffer_len, "--/-- --:--");
    return;
  }

  strftime(buffer, buffer_len, "%m/%d %H:%M", tm_info);
}

static uint16_t main_menu_get_num_sections(MenuLayer *menu_layer, void *context) {
  (void)menu_layer;
  (void)context;
  return 1;
}

static uint16_t main_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  (void)menu_layer;
  (void)section_index;
  (void)context;
  return 3;
}

static void main_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  (void)context;

  if (cell_index->row == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "New Game", "Start fresh from 0", NULL);
    return;
  }

  if (cell_index->row == 1) {
    if (s_store.game_count == 0) {
      menu_cell_basic_draw(ctx, cell_layer, "Continue Game", "No saved games", NULL);
    } else {
      static char subtitle[24];
      snprintf(subtitle, sizeof(subtitle), "%d saved game%s", s_store.game_count,
               s_store.game_count == 1 ? "" : "s");
      menu_cell_basic_draw(ctx, cell_layer, "Continue Game", subtitle, NULL);
    }
    return;
  }

  static char settings_subtitle[24];
  snprintf(settings_subtitle, sizeof(settings_subtitle), "Score Step: %d", score_step_get());
  menu_cell_basic_draw(ctx, cell_layer, "Settings", settings_subtitle, NULL);
}

static void main_menu_select(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  (void)menu_layer;
  (void)context;

  if (cell_index->row == 0) {
    game_store_start_new_game();
    window_stack_push(s_game_window, true);
    return;
  }

  if (cell_index->row == 1) {
    if (s_store.game_count == 0) {
      vibes_short_pulse();
      return;
    }

    if (s_continue_menu_layer) {
      menu_layer_reload_data(s_continue_menu_layer);
    }
    window_stack_push(s_continue_menu_window, true);
    return;
  }

  if (s_settings_menu_layer) {
    menu_layer_reload_data(s_settings_menu_layer);
  }
  window_stack_push(s_settings_window, true);
}

static uint16_t settings_menu_get_num_sections(MenuLayer *menu_layer, void *context) {
  (void)menu_layer;
  (void)context;
  return 1;
}

static uint16_t settings_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  (void)menu_layer;
  (void)section_index;
  (void)context;
  return SCORE_STEP_OPTION_COUNT;
}

static void settings_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  (void)context;

  int row = cell_index->row;
  if (row < 0 || row >= SCORE_STEP_OPTION_COUNT) {
    menu_cell_basic_draw(ctx, cell_layer, "Invalid", "", NULL);
    return;
  }

  int step = SCORE_STEP_OPTIONS[row];
  int current_step = score_step_get();

  static char title[24];
  snprintf(title, sizeof(title), "Score Step: %d", step);
  menu_cell_basic_draw(ctx, cell_layer, title,
                      step == current_step ? "Selected" : "Tap to apply", NULL);
}

static void settings_menu_select(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  (void)menu_layer;
  (void)context;

  int row = cell_index->row;
  if (row < 0 || row >= SCORE_STEP_OPTION_COUNT) {
    return;
  }

  int next_step = score_step_normalize((int)SCORE_STEP_OPTIONS[row]);
  int current_step = score_step_get();

  if (next_step != current_step) {
    score_step_set(next_step);
    game_store_save();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Settings: score step set to %d", next_step);
  }

  if (s_main_menu_layer) {
    menu_layer_reload_data(s_main_menu_layer);
  }
  window_stack_pop(true);
}

static bool replay_track_has_playable_data(uint8_t game_index) {
  if (game_index >= MAX_SAVED_GAMES) {
    return false;
  }
  return s_replay_tracks[game_index].count > 1;
}

static uint16_t game_action_get_num_sections(MenuLayer *menu_layer, void *context) {
  (void)menu_layer;
  (void)context;
  return 1;
}

static uint16_t game_action_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  (void)menu_layer;
  (void)section_index;
  (void)context;
  return 3;
}

static void game_action_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  (void)context;

  if (cell_index->row == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "Continue", "Resume live game", NULL);
    return;
  }

  if (cell_index->row == 1) {
    if (replay_track_has_playable_data(s_selected_continue_index)) {
      menu_cell_basic_draw(ctx, cell_layer, "Replay", "Watch checkpoints", NULL);
    } else {
      menu_cell_basic_draw(ctx, cell_layer, "Replay", "No checkpoints yet", NULL);
    }
    return;
  }

  menu_cell_basic_draw(ctx, cell_layer, "Back", "Return to saves", NULL);
}

static void replay_open_selected_game(void) {
  if (s_selected_continue_index >= s_store.game_count) {
    return;
  }

  s_replay_source_index = s_selected_continue_index;
  s_replay_seed_game = s_store.games[s_replay_source_index];
  s_replay_view_game = s_replay_seed_game;
  s_replay_mode = true;
  s_replay_complete = false;
  score_delta_clear();

  window_stack_push(s_replay_window, true);
  replay_begin_playback();
}

static void game_action_select(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  (void)menu_layer;
  (void)context;

  if (s_selected_continue_index >= s_store.game_count) {
    return;
  }

  if (cell_index->row == 0) {
    game_store_continue_game(s_selected_continue_index);
    window_stack_pop(true);
    window_stack_push(s_game_window, true);
    return;
  }

  if (cell_index->row == 1) {
    if (!replay_track_has_playable_data(s_selected_continue_index)) {
      vibes_short_pulse();
      return;
    }
    replay_open_selected_game();
    return;
  }

  window_stack_pop(true);
}

static void prv_replay_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  if (!s_replay_complete) {
    replay_finish_playback_now();
    vibes_short_pulse();
    return;
  }

  uint8_t source_index = s_replay_source_index;
  replay_cancel_timer();
  s_replay_mode = false;
  score_delta_clear();

  window_stack_pop(false);
  if (s_game_action_window) {
    window_stack_remove(s_game_action_window, false);
  }

  s_consume_next_game_select = true;
  game_store_continue_game(source_index);
  window_stack_push(s_game_window, true);
}

static void prv_replay_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;

  if (!s_replay_complete) {
    return;
  }

  replay_begin_playback();
}

static void prv_replay_click_config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_replay_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prv_replay_up_click_handler);
}

static void prv_game_action_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int title_height = 24;
  const int title_y = ROUND_TITLE_TOP_PADDING;
  const int menu_top = title_y + title_height + 2;
  static char title_text[24];

  window_set_background_color(window, GColorBlack);

  snprintf(title_text, sizeof(title_text), "Game %d", s_selected_continue_index + 1);

  s_game_action_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, title_height));
  text_layer_set_text(s_game_action_title_layer, title_text);
  text_layer_set_text_alignment(s_game_action_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_game_action_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_game_action_title_layer, GColorBlack);
  text_layer_set_text_color(s_game_action_title_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_game_action_title_layer));

  s_game_action_menu_layer = menu_layer_create(GRect(0, menu_top, bounds.size.w, bounds.size.h - menu_top));
  menu_layer_set_callbacks(s_game_action_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = game_action_get_num_sections,
    .get_num_rows = game_action_get_num_rows,
    .draw_row = game_action_draw_row,
    .select_click = game_action_select,
  });

  menu_layer_set_normal_colors(s_game_action_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_game_action_menu_layer, GColorCobaltBlue, GColorWhite);
  menu_layer_set_click_config_onto_window(s_game_action_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_game_action_menu_layer));
}

static void prv_game_action_window_unload(Window *window) {
  (void)window;
  if (s_game_action_title_layer) {
    text_layer_destroy(s_game_action_title_layer);
    s_game_action_title_layer = NULL;
  }
  if (s_game_action_menu_layer) {
    menu_layer_destroy(s_game_action_menu_layer);
    s_game_action_menu_layer = NULL;
  }
}

static void prv_replay_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_replay_layer = layer_create(bounds);
  layer_set_update_proc(s_replay_layer, prv_game_layer_update_proc);
  layer_add_child(window_layer, s_replay_layer);

  window_set_background_color(window, GColorBlack);
  render_player_scores();
}

static void prv_replay_window_unload(Window *window) {
  (void)window;
  replay_cancel_timer();

  if (s_replay_layer) {
    layer_destroy(s_replay_layer);
    s_replay_layer = NULL;
  }

  s_replay_mode = false;
  s_replay_complete = false;
  s_consume_next_game_select = false;
  score_delta_clear();
}

static uint16_t continue_menu_get_num_sections(MenuLayer *menu_layer, void *context) {
  (void)menu_layer;
  (void)context;
  return 1;
}

static uint16_t continue_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
  (void)menu_layer;
  (void)section_index;
  (void)context;
  return s_store.game_count > 0 ? s_store.game_count : 1;
}

static void continue_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  (void)context;

  if (s_store.game_count == 0) {
    menu_cell_basic_draw(ctx, cell_layer, "No saved games", "Press Back", NULL);
    return;
  }

  GameSession *game = &s_store.games[cell_index->row];
  static char title[32];
  static char subtitle[32];
  static char created[16];
  static char modified[16];

  format_timestamp_short(game->created_at, created, sizeof(created));
  format_timestamp_short(game->last_modified, modified, sizeof(modified));

  snprintf(title, sizeof(title), "Game %d  C:%s", cell_index->row + 1, created);
  snprintf(subtitle, sizeof(subtitle), "Last Played: %s", modified);
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void continue_menu_select(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  (void)menu_layer;
  (void)context;

  if (s_store.game_count == 0) {
    return;
  }

  s_selected_continue_index = (uint8_t)cell_index->row;
  if (s_game_action_menu_layer) {
    menu_layer_reload_data(s_game_action_menu_layer);
  }
  window_stack_push(s_game_action_window, true);
}

static void prv_main_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int title_height = 24;
  const int title_y = ROUND_TITLE_TOP_PADDING;
  const int menu_top = title_y + title_height + 2;

  window_set_background_color(window, GColorBlack);

  s_main_menu_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, title_height));
  text_layer_set_text(s_main_menu_title_layer, "Pebble Points");
  text_layer_set_text_alignment(s_main_menu_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_main_menu_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_main_menu_title_layer, GColorBlack);
  text_layer_set_text_color(s_main_menu_title_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_main_menu_title_layer));

  s_main_menu_layer = menu_layer_create(GRect(0, menu_top, bounds.size.w, bounds.size.h - menu_top));
  menu_layer_set_callbacks(s_main_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = main_menu_get_num_sections,
    .get_num_rows = main_menu_get_num_rows,
    .draw_row = main_menu_draw_row,
    .select_click = main_menu_select,
  });

  menu_layer_set_normal_colors(s_main_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_main_menu_layer, GColorCobaltBlue, GColorWhite);

  menu_layer_set_click_config_onto_window(s_main_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_main_menu_layer));
}

static void prv_main_menu_window_unload(Window *window) {
  (void)window;
  if (s_main_menu_title_layer) {
    text_layer_destroy(s_main_menu_title_layer);
    s_main_menu_title_layer = NULL;
  }
  if (s_main_menu_layer) {
    menu_layer_destroy(s_main_menu_layer);
    s_main_menu_layer = NULL;
  }
}

static void prv_settings_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int title_height = 24;
  const int title_y = ROUND_TITLE_TOP_PADDING;
  const int menu_top = title_y + title_height + 2;

  window_set_background_color(window, GColorBlack);

  s_settings_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, title_height));
  text_layer_set_text(s_settings_title_layer, "Settings");
  text_layer_set_text_alignment(s_settings_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_settings_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_settings_title_layer, GColorBlack);
  text_layer_set_text_color(s_settings_title_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_settings_title_layer));

  s_settings_menu_layer = menu_layer_create(GRect(0, menu_top, bounds.size.w, bounds.size.h - menu_top));
  menu_layer_set_callbacks(s_settings_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = settings_menu_get_num_sections,
    .get_num_rows = settings_menu_get_num_rows,
    .draw_row = settings_menu_draw_row,
    .select_click = settings_menu_select,
  });

  menu_layer_set_normal_colors(s_settings_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_settings_menu_layer, GColorOrange, GColorWhite);

  menu_layer_set_click_config_onto_window(s_settings_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_settings_menu_layer));
}

static void prv_settings_window_unload(Window *window) {
  (void)window;
  if (s_settings_title_layer) {
    text_layer_destroy(s_settings_title_layer);
    s_settings_title_layer = NULL;
  }
  if (s_settings_menu_layer) {
    menu_layer_destroy(s_settings_menu_layer);
    s_settings_menu_layer = NULL;
  }
}

static void prv_continue_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int title_height = 24;
  const int title_y = ROUND_TITLE_TOP_PADDING;
  const int menu_top = title_y + title_height + 2;

  window_set_background_color(window, GColorBlack);

  s_continue_menu_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, title_height));
  text_layer_set_text(s_continue_menu_title_layer, "Pebble Points");
  text_layer_set_text_alignment(s_continue_menu_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_continue_menu_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_continue_menu_title_layer, GColorBlack);
  text_layer_set_text_color(s_continue_menu_title_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_continue_menu_title_layer));

  s_continue_menu_layer = menu_layer_create(GRect(0, menu_top, bounds.size.w, bounds.size.h - menu_top));
  menu_layer_set_callbacks(s_continue_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = continue_menu_get_num_sections,
    .get_num_rows = continue_menu_get_num_rows,
    .draw_row = continue_menu_draw_row,
    .select_click = continue_menu_select,
  });

  menu_layer_set_normal_colors(s_continue_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_continue_menu_layer, GColorVividViolet, GColorWhite);

  menu_layer_set_click_config_onto_window(s_continue_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_continue_menu_layer));
}

static void prv_continue_menu_window_unload(Window *window) {
  (void)window;
  if (s_continue_menu_title_layer) {
    text_layer_destroy(s_continue_menu_title_layer);
    s_continue_menu_title_layer = NULL;
  }
  if (s_continue_menu_layer) {
    menu_layer_destroy(s_continue_menu_layer);
    s_continue_menu_layer = NULL;
  }
}

static void prv_game_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  game_store_ensure_active();

  // Generate layout configuration based on player count
  s_layout = layout_get_config(bounds, s_game.player_count);

  // Create game layer with custom rendering function (all rendering happens here)
  s_game_layer = layer_create(bounds);
  layer_set_update_proc(s_game_layer, prv_game_layer_update_proc);
  layer_add_child(window_layer, s_game_layer);
  
  // Initialize text buffers with player names and scores
  render_player_scores();
  
  // Set background color
  window_set_background_color(window, GColorBlack);

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Window loaded. Active player: %d, Player count: %d", 
          s_game.active_index, s_game.player_count);
}

static void prv_game_window_unload(Window *window) {
  (void)window;
  // Cancel any active animations
  animation_cancel_active();
  
  // Clean up confetti
  if (s_confetti_ctx.cleanup_timer) {
    app_timer_cancel(s_confetti_ctx.cleanup_timer);
    s_confetti_ctx.cleanup_timer = NULL;
    memset(&s_confetti_ctx, 0, sizeof(s_confetti_ctx));
  }
  
  // Clean up floating texts
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (s_floating_texts[i].floating_text) {
      floating_text_cleanup(&s_floating_texts[i]);
    }
  }
  
  // Clean up game layer
  if (s_game_layer) {
    layer_destroy(s_game_layer);
    s_game_layer = NULL;
  }
  
  // Save state on app exit
  game_session_save(&s_game);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Window unloaded. Game state saved.");
}

// ============================================================================
// PHASE 5: Phone Settings & AppMessage (Steps 15-17)
// ============================================================================

// Auto-generated message keys from config.json (defined in message_keys.auto.h)

// Handle incoming AppMessage from phone
static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "AppMessage received from phone");

  // Ensure there is always an active game slot to apply settings to.
  game_store_ensure_active();
  
  // Process settings from phone
  // Note: Full implementation would parse each key and update s_game state
  // For now, this is a placeholder that validates the message received
  
  Tuple *player_count_tuple = dict_find(iterator, MESSAGE_KEY_playerCount);
  if (player_count_tuple) {
    int new_count = s_game.player_count;
    if (player_count_tuple->type == TUPLE_CSTRING) {
      new_count = atoi(player_count_tuple->value->cstring);
    } else {
      new_count = player_count_tuple->value->int32;
    }

    // Validate player count is in valid range (2-4)
    if (new_count >= 2 && new_count <= 4) {
      s_game.player_count = new_count;
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated player count to: %d", new_count);
      // Reset active index if needed
      if (s_game.active_index >= s_game.player_count) {
        s_game.active_index = 0;
      }
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Invalid player count from phone: %d (must be 2-4)", new_count);
    }
  }
  
  // Check for player name updates
  // Message keys: MESSAGE_KEY_player1Name, MESSAGE_KEY_player2Name, etc.
  for (int i = 0; i < MAX_PLAYERS; i++) {
    uint32_t name_key;
    switch(i) {
      case 0: name_key = MESSAGE_KEY_player1Name; break;
      case 1: name_key = MESSAGE_KEY_player2Name; break;
      case 2: name_key = MESSAGE_KEY_player3Name; break;
      case 3: name_key = MESSAGE_KEY_player4Name; break;
      default: continue;
    }
    
    Tuple *name_tuple = dict_find(iterator, name_key);
    if (name_tuple && name_tuple->type == TUPLE_CSTRING) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated player %d name to: %s", i, name_tuple->value->cstring);
      snprintf(s_game.players[i].name, MAX_NAME_LEN, "%s", name_tuple->value->cstring);
    }
  }
  
  // Check for feature toggles
  Tuple *confetti_tuple = dict_find(iterator, MESSAGE_KEY_enableConfetti);
  if (confetti_tuple) {
    s_game.enable_confetti = confetti_tuple->value->int32 ? 1 : 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Confetti setting: %d", s_game.enable_confetti);
  }
  
  Tuple *haptics_tuple = dict_find(iterator, MESSAGE_KEY_enableHaptics);
  if (haptics_tuple) {
    s_game.enable_haptics = haptics_tuple->value->int32 ? 1 : 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Haptics setting: %d", s_game.enable_haptics);
  }
  
  // Save updated game state
  game_session_save(&s_game);

  if (s_last_delta_player >= s_game.player_count) {
    score_delta_clear();
  }

  // Refresh cached text buffers and redraw UI so updated names appear immediately.
  render_player_scores();
}

// Handle AppMessage errors
static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage dropped: error code %d", reason);
}

static void prv_init(void) {
  // Register AppMessage handlers (Step 17)
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  
  // Set up inbox/outbox sizes
  // Note: Minimum size needed for Clay settings + some buffer
  const uint32_t inbox_size = 256;
  const uint32_t outbox_size = 64;
  app_message_open(inbox_size, outbox_size);

  // Load persisted game store (or migrate legacy single-game format).
  game_store_load();

  s_main_menu_window = window_create();
  window_set_window_handlers(s_main_menu_window, (WindowHandlers) {
    .load = prv_main_menu_window_load,
    .unload = prv_main_menu_window_unload,
  });

  s_continue_menu_window = window_create();
  window_set_window_handlers(s_continue_menu_window, (WindowHandlers) {
    .load = prv_continue_menu_window_load,
    .unload = prv_continue_menu_window_unload,
  });

  s_settings_window = window_create();
  window_set_window_handlers(s_settings_window, (WindowHandlers) {
    .load = prv_settings_window_load,
    .unload = prv_settings_window_unload,
  });

  s_game_action_window = window_create();
  window_set_window_handlers(s_game_action_window, (WindowHandlers) {
    .load = prv_game_action_window_load,
    .unload = prv_game_action_window_unload,
  });

  s_replay_window = window_create();
  window_set_click_config_provider(s_replay_window, prv_replay_click_config_provider);
  window_set_window_handlers(s_replay_window, (WindowHandlers) {
    .load = prv_replay_window_load,
    .unload = prv_replay_window_unload,
  });
  
  s_game_window = window_create();
  window_set_click_config_provider(s_game_window, prv_click_config_provider);
  window_set_window_handlers(s_game_window, (WindowHandlers) {
    .load = prv_game_window_load,
    .unload = prv_game_window_unload,
  });
  
  // Push startup menu onto stack.
  const bool animated = true;
  window_stack_push(s_main_menu_window, animated);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App initialized");
}

static void prv_deinit(void) {
  replay_cancel_timer();
  window_destroy(s_game_window);
  window_destroy(s_replay_window);
  window_destroy(s_game_action_window);
  window_destroy(s_settings_window);
  window_destroy(s_continue_menu_window);
  window_destroy(s_main_menu_window);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App deinitialized");
}

int main(void) {
  prv_init();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Starting event loop");
  
  app_event_loop();
  
  prv_deinit();
  
  return 0;
}
