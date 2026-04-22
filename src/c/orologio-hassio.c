#include <pebble.h>

#define KEY_SENSOR_VALUE 0
#define KEY_LIGHT_INDEX 1
#define KEY_LIGHT_NAME 2
#define KEY_LIGHT_STATE 3
#define KEY_LIGHT_ENTITY 4
#define KEY_TOGGLE_LIGHT 5
#define KEY_SENSOR_UNIT 6
#define KEY_SENSOR_NAME 7
#define KEY_CHAT_TEXT 8
#define KEY_CHAT_ACTIVE 9
#define KEY_SEND_CHAT 10
#define KEY_NICKNAME 11
#define KEY_LAUNCH_APP 12
#define KEY_MUSIC_TITLE 13
#define KEY_CONFIG_OPEN 14
#define KEY_GET_HISTORY 15
#define KEY_HISTORY_DATA 16 // Dati campionati per il grafico
#define KEY_GLOBAL_MAX 17   // Valore massimo assoluto del periodo
#define KEY_GLOBAL_MIN 18   // Valore minimo assoluto del periodo
 
static Window *s_window;
static Window *s_menu_window;
static MenuLayer *s_menu_layer;
static Window *s_music_window;
static Window *s_graph_window;
static Layer *s_graph_layer;
static TextLayer *s_graph_title_layer; // Combined MAX/MIN value
static TextLayer *s_graph_min_layer;   // Layer per il valore minimo

static int s_history_visual_min = 0;   // Valore minimo per la scala del grafico
static int s_history_visual_max = 100; // Valore massimo per la scala del grafico
static int s_history_duration_hours = 6; // Default 6 ore
static int s_history_data[40];
static int s_history_count = 0;

static TextLayer *s_countdown_layer;
static TextLayer *s_sensor_name_layer;
static char s_graph_max_str[128]; // Aumentato per 3 righe di testo
static char s_graph_min_str[64];  // Buffer per il valore minimo
static char s_unit_str[16] = "";  // Buffer per l'unità di misura
static Layer *s_chart_layer;
static int s_target_percent = 0;
static int s_start_percent = 0;
static int s_sensor_percent = 0;
static char s_sensor_value_str[32] = "Caricamento...";
static char s_sensor_name_str[64] = "";

static AppTimer *s_feedback_timer;
static AppTimer *s_auto_close_timer;

typedef struct {
  char name[32];
  char entity[64];
  char state[16];
} Light;

static Light s_lights[10];
static int s_num_lights = 0;
static int s_display_order[10]; // Mappa indice menu -> indice s_lights

// --- Chat Window Variables ---
static Window *s_chat_window;
static ScrollLayer *s_chat_scroll_layer;
static GRect s_chat_bounds;
#define MAX_CHAT_LAYERS 5
static TextLayer *s_chat_layers[MAX_CHAT_LAYERS];
static char *s_chat_texts[MAX_CHAT_LAYERS];
static int s_chat_layer_count = 0;
static int s_chat_content_height = 0;
#if defined(PBL_MICROPHONE)
static DictationSession *s_dictation_session;
#endif
static char s_last_dictation[512];

// --- Canned Messages ---
static Window *s_canned_window;
static MenuLayer *s_canned_menu_layer;
static char *s_canned_messages[] = {
  "Dettatura", 
  "Sì", 
  "No", 
  "Arrivo", 
  "Ok", 
  "Grazie"
};
static char s_pebble_nickname[32] = "Pebble";

static void reset_auto_close_timer(void);
static void auto_close_callback(void *data);

static void send_chat_active(bool active) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, KEY_CHAT_ACTIVE, active ? 1 : 0);
    app_message_outbox_send();
  }
}

static GColor get_color_for_nickname(char *text) {
#if defined(PBL_COLOR)
  // Cerca l'ultima parentesi aperta e chiusa per trovare il nickname
  char *start = strrchr(text, '(');
  char *end = strrchr(text, ')');
  
  if (start && end && end > start) {
    uint32_t hash = 0;
    // Calcola un hash semplice basato sui caratteri del nickname
    for (char *p = start + 1; p < end; p++) {
      hash = hash * 31 + *p;
    }
    
    // Palette di colori chiari per garantire leggibilità con testo nero
    GColor colors[] = {
      GColorChromeYellow, GColorCeleste, GColorMintGreen, 
      GColorPastelYellow, GColorMelon, GColorRichBrilliantLavender, 
      GColorSunsetOrange, GColorPictonBlue
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);
    return colors[hash % num_colors];
  }
#endif
  return GColorWhite; // Colore di default se non trova nickname
}

static void add_chat_message(char *new_text) {
  if (!s_chat_scroll_layer) return;

  // Controlla se il messaggio inizia con '>' per allinearlo a destra
  bool is_me = (new_text[0] == '>');
  char *text_to_show = is_me ? new_text + 1 : new_text;
  size_t len = strlen(text_to_show);

  // Se abbiamo raggiunto il limite, rimuovi il messaggio più vecchio
  if (s_chat_layer_count >= MAX_CHAT_LAYERS) {
    // Recupera l'altezza del primo messaggio per shiftare gli altri
    GRect frame = layer_get_frame(text_layer_get_layer(s_chat_layers[0]));
    int shift_amount = frame.size.h;

    // Distruggi il layer e libera la memoria del testo
    text_layer_destroy(s_chat_layers[0]);
    free(s_chat_texts[0]);

    // Shifta gli array
    for (int i = 0; i < s_chat_layer_count - 1; i++) {
      s_chat_layers[i] = s_chat_layers[i+1];
      s_chat_texts[i] = s_chat_texts[i+1];
      
      // Sposta visivamente il layer verso l'alto
      GRect f = layer_get_frame(text_layer_get_layer(s_chat_layers[i]));
      f.origin.y -= shift_amount;
      layer_set_frame(text_layer_get_layer(s_chat_layers[i]), f);
    }
    s_chat_layer_count--;
    s_chat_content_height -= shift_amount;
  }

  // Alloca memoria per il nuovo messaggio
  char *copy = malloc(len + 1);
  if (copy) {
    strcpy(copy, text_to_show);
    
    // Rimuovi il nickname per la visualizzazione
    char *start_nick = strrchr(copy, '(');
    char *end_nick = strrchr(copy, ')');
    if (start_nick && end_nick && end_nick > start_nick) {
      // Rimuove anche lo spazio precedente se presente
      if (start_nick > copy && *(start_nick - 1) == ' ') {
        *(start_nick - 1) = '\0';
      } else {
        *start_nick = '\0';
      }
    }
    
    int16_t width = s_chat_bounds.size.w;

    // Crea un nuovo TextLayer per questo messaggio
    TextLayer *tl = text_layer_create(GRect(0, s_chat_content_height, width, 2000));
    text_layer_set_text(tl, copy);
    text_layer_set_font(tl, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(tl, is_me ? GTextAlignmentRight : GTextAlignmentLeft);
    text_layer_set_background_color(tl, get_color_for_nickname(new_text));
    text_layer_set_text_color(tl, GColorBlack);

    // Calcola l'altezza necessaria e ridimensiona il layer
    GSize size = text_layer_get_content_size(tl);
    text_layer_set_size(tl, GSize(width, size.h));
    
    scroll_layer_add_child(s_chat_scroll_layer, text_layer_get_layer(tl));
    s_chat_texts[s_chat_layer_count] = copy;
    s_chat_layers[s_chat_layer_count++] = tl;
    
    // Aggiorna offset e altezza totale
    s_chat_content_height += size.h;
    
    scroll_layer_set_content_size(s_chat_scroll_layer, GSize(width, s_chat_content_height));
    
    int16_t view_h = s_chat_bounds.size.h;
    if (s_chat_content_height > view_h) {
      scroll_layer_set_content_offset(s_chat_scroll_layer, GPoint(0, -(s_chat_content_height - view_h)), true);
    } else {
      scroll_layer_set_content_offset(s_chat_scroll_layer, GPoint(0, PBL_IF_ROUND_ELSE(view_h/4, 0)), true);
    }

    // Vibrazione all'arrivo di un nuovo messaggio
    vibes_short_pulse();
  }
}

#if defined(PBL_MICROPHONE)
static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    // Aggiunge il prefisso > e invia
    snprintf(s_last_dictation, sizeof(s_last_dictation), ">%s (%s)", transcription, s_pebble_nickname);
    
    // Aggiorna immediatamente la UI locale
    add_chat_message(s_last_dictation);

    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_cstring(iter, KEY_SEND_CHAT, s_last_dictation);
      app_message_outbox_send();
    }
    
    // Se la finestra dei messaggi predefiniti è aperta, chiudila per mostrare la chat
    if (s_canned_window && window_stack_get_top_window() == s_canned_window) {
      window_stack_pop(true);
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Dictation failed: %d", (int)status);
  }
}
#endif

static void chat_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  GPoint offset = scroll_layer_get_content_offset(s_chat_scroll_layer);
  offset.y += s_chat_bounds.size.h / 2;
  if (offset.y > 0) offset.y = 0;
  scroll_layer_set_content_offset(s_chat_scroll_layer, offset, true);
}

static void chat_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  GPoint offset = scroll_layer_get_content_offset(s_chat_scroll_layer);
  GSize size = scroll_layer_get_content_size(s_chat_scroll_layer);
  int view_h = s_chat_bounds.size.h; 
  int min_y = view_h - size.h;
  if (min_y > 0) min_y = 0;
  
  offset.y -= view_h / 2;
  if (offset.y < min_y) offset.y = min_y;
  scroll_layer_set_content_offset(s_chat_scroll_layer, offset, true);
}

static uint16_t canned_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return sizeof(s_canned_messages) / sizeof(s_canned_messages[0]);
}

static void canned_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_canned_messages[cell_index->row], NULL, NULL);
}

static void canned_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int row = cell_index->row;
  
  if (row == 0) {
    // Dettatura
    #if defined(PBL_MICROPHONE)
    if (s_dictation_session) {
      dictation_session_start(s_dictation_session);
    }
    #endif
  } else {
    // Messaggio predefinito
    char *msg = s_canned_messages[row];
    
    snprintf(s_last_dictation, sizeof(s_last_dictation), ">%s (%s)", msg, s_pebble_nickname);
    add_chat_message(s_last_dictation);

    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_cstring(iter, KEY_SEND_CHAT, s_last_dictation);
      app_message_outbox_send();
    }
    
    window_stack_pop(true);
  }
}

static void canned_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_canned_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_canned_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = canned_menu_get_num_rows_callback,
    .draw_row = canned_menu_draw_row_callback,
    .select_click = canned_menu_select_callback,
  });
  
  menu_layer_set_click_config_onto_window(s_canned_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_canned_menu_layer));
}

static void canned_window_unload(Window *window) {
  menu_layer_destroy(s_canned_menu_layer);
  s_canned_menu_layer = NULL;
}

static void chat_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (!s_canned_window) {
    s_canned_window = window_create();
    window_set_window_handlers(s_canned_window, (WindowHandlers) {
      .load = canned_window_load,
      .unload = canned_window_unload,
    });
  }
  window_stack_push(s_canned_window, true);
}

static void chat_click_config_provider(void *context) {
  // Gestione manuale dello scroll per permettere l'uso del tasto Select per aprire i messaggi predefiniti
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, chat_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, chat_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, chat_select_click_handler);
}

static void chat_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_chat_bounds = layer_get_bounds(window_layer);

  // Pulisce il buffer all'apertura
  s_chat_layer_count = 0;
  s_chat_content_height = 0;

  s_chat_scroll_layer = scroll_layer_create(s_chat_bounds);
  
  // Impostiamo il provider personalizzato invece di quello standard di ScrollLayer
  window_set_click_config_provider(window, chat_click_config_provider);

  scroll_layer_set_content_size(s_chat_scroll_layer, GSize(s_chat_bounds.size.w, 0));

  layer_add_child(window_layer, scroll_layer_get_layer(s_chat_scroll_layer));

  // Inizializza sessione di dettatura
  #if defined(PBL_MICROPHONE)
  s_dictation_session = dictation_session_create(sizeof(s_last_dictation), dictation_session_callback, NULL);
  dictation_session_enable_confirmation(s_dictation_session, true);
  #endif

  // Avvisa JS che la chat è attiva
  send_chat_active(true);

  // Disabilita il timeout di chiusura automatica nella chat
  if (s_auto_close_timer) {
    app_timer_cancel(s_auto_close_timer);
    s_auto_close_timer = NULL;
  }
}

static void chat_window_unload(Window *window) {
  // Avvisa JS che la chat è disattivata
  send_chat_active(false);
  
  // Riavvia il timer quando si esce dalla chat
  reset_auto_close_timer();
  
  #if defined(PBL_MICROPHONE)
  if (s_dictation_session) {
    dictation_session_destroy(s_dictation_session);
    s_dictation_session = NULL;
  }
  #endif
  
  for (int i = 0; i < s_chat_layer_count; i++) {
    text_layer_destroy(s_chat_layers[i]);
    free(s_chat_texts[i]);
  }
  scroll_layer_destroy(s_chat_scroll_layer);
  s_chat_scroll_layer = NULL;
}

// --- Music Window ---
static TextLayer *s_music_layer;
static TextLayer *s_music_title_layer;
static char s_music_title_str[64] = "Media Player";

static void music_send_command(char *cmd) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_cstring(iter, KEY_LAUNCH_APP, cmd);
    app_message_outbox_send();
    vibes_short_pulse();
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Music command failed: outbox busy");
  }
}

static void music_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  music_send_command("music_prev");
}

static void music_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  music_send_command("music_play_pause");
}

static void music_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  music_send_command("music_next");
}

static void music_up_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  music_send_command("volume_up");
}

static void music_down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  music_send_command("volume_down");
}

static void music_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, music_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, music_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, music_down_click_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, music_up_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, music_down_long_click_handler, NULL);
}

// Wrapper per il callback del timer che rispetta la firma corretta
static void music_timer_callback(void *data) {
  music_send_command((char*)data);
}

static void music_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Layer Titolo (in alto)
  s_music_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, 80));
  text_layer_set_text(s_music_title_layer, s_music_title_str);
  text_layer_set_text_alignment(s_music_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_music_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(s_music_title_layer, GColorBlack);
  text_layer_set_text_color(s_music_title_layer, GColorWhite);
  text_layer_set_overflow_mode(s_music_title_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_music_title_layer));

  // Layer Controlli (in basso)
  s_music_layer = text_layer_create(GRect(0, 80, bounds.size.w, bounds.size.h - (PBL_IF_ROUND_ELSE(100, 80))));
  text_layer_set_text(s_music_layer, "UP: < / Vol+\nSEL: Play/Pause\nDOWN: > / Vol-");
  text_layer_set_text_alignment(s_music_layer, GTextAlignmentCenter);
  text_layer_set_font(s_music_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_music_layer, GColorBlack);
  text_layer_set_text_color(s_music_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_music_layer));
  
  window_set_click_config_provider(window, music_click_config_provider);

  // Richiedi aggiornamento stato musica all'apertura
  app_timer_register(1500, music_timer_callback, "get_music_info");
}

static void music_window_unload(Window *window) {
  text_layer_destroy(s_music_layer);
  s_music_layer = NULL;
  text_layer_destroy(s_music_title_layer);
  s_music_title_layer = NULL;
}
// -----------------------------

static void chart_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  // Ridimensiona lo spessore del gauge in base alla dimensione dello schermo
  int thickness = bounds.size.w / 7;
  
  // Su schermi rotondi, restringiamo leggermente il raggio per non toccare i bordi
  GRect inset_bounds = PBL_IF_ROUND_ELSE(grect_inset(bounds, GEdgeInsets(5)), bounds);
  
  // Angoli per il semicerchio superiore (da ore 9 a ore 3 in senso orario)
  int32_t start_angle = TRIG_MAX_ANGLE * 3 / 4;
  int32_t total_span = TRIG_MAX_ANGLE / 2;

  #if defined(PBL_COLOR)
  // 1. Disegna i segmenti di severità a spessore pieno (riempiono tutto il semicerchio)
  graphics_context_set_fill_color(ctx, GColorGreen);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle, start_angle + (total_span / 2));
  
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle + (total_span / 2), start_angle + (3 * total_span / 4));
  
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle + (3 * total_span / 4), start_angle + total_span);
  #else
  // 1. Disegna i segmenti con tre tonalità distinte (Bianco, Grigio, Nero)
  // Su Pebble B&W, Light e Dark Gray sono identici, quindi usiamo il Bianco per la zona "safe".
  // Primo segmento: Bianco
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle, start_angle + (total_span / 2));
  
  // Secondo segmento: Grigio (Dithering 50%)
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle + (total_span / 2), start_angle + (3 * total_span / 4));
  
  // Terzo segmento: Nero pieno
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, thickness, start_angle + (3 * total_span / 4), start_angle + total_span);

  // Aggiungiamo un contorno nero per definire la forma del gauge (essenziale per vedere il bianco)
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_arc(ctx, inset_bounds, GOvalScaleModeFitCircle, start_angle, start_angle + total_span);
  graphics_draw_arc(ctx, grect_inset(inset_bounds, GEdgeInsets(thickness)), GOvalScaleModeFitCircle, start_angle, start_angle + total_span);
  graphics_draw_line(ctx, GPoint(inset_bounds.origin.x, inset_bounds.size.h / 2), GPoint(inset_bounds.origin.x + thickness, inset_bounds.size.h / 2));
  graphics_draw_line(ctx, GPoint(inset_bounds.origin.x + inset_bounds.size.w - thickness, inset_bounds.size.h / 2), GPoint(inset_bounds.origin.x + inset_bounds.size.w, inset_bounds.size.h / 2));
  #endif

  // 2. Disegna i segni di spunta (ticks) bianchi per separare i segmenti
  GPoint center = GPoint(inset_bounds.origin.x + inset_bounds.size.w / 2, inset_bounds.origin.y + inset_bounds.size.h / 2);
  uint16_t radius = inset_bounds.size.w / 2;
  
  // Sui modelli B&W usiamo il nero per i ticks, altrimenti sarebbero invisibili sul bianco/grigio
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  graphics_context_set_stroke_width(ctx, 2);
  
  int32_t tick_angles[2] = {
    start_angle + (total_span / 2),
    start_angle + (3 * total_span / 4)
  };

  for (int i = 0; i < 2; i++) {
    GPoint p1 = {
      .x = (int16_t)(sin_lookup(tick_angles[i]) * (int32_t)(radius - thickness) / TRIG_MAX_RATIO) + center.x,
      .y = (int16_t)(-cos_lookup(tick_angles[i]) * (int32_t)(radius - thickness) / TRIG_MAX_RATIO) + center.y,
    };
    GPoint p2 = {
      .x = (int16_t)(sin_lookup(tick_angles[i]) * (int32_t)radius / TRIG_MAX_RATIO) + center.x,
      .y = (int16_t)(-cos_lookup(tick_angles[i]) * (int32_t)radius / TRIG_MAX_RATIO) + center.y,
    };
    graphics_draw_line(ctx, p1, p2);
  }

  // 3. Disegna la lancetta (needle)
  int32_t val_angle = start_angle + (total_span * s_sensor_percent / 100);
  // Clamp dell'angolo per sicurezza
  if (s_sensor_percent >= 100) val_angle = start_angle + total_span;
  if (s_sensor_percent <= 0) val_angle = start_angle;
  // Calcoliamo la posizione della punta della lancetta usando seno e coseno
  GPoint needle_end = {
    .x = (int16_t)(sin_lookup(val_angle) * (int32_t)radius / TRIG_MAX_RATIO) + center.x,
    .y = (int16_t)(-cos_lookup(val_angle) * (int32_t)radius / TRIG_MAX_RATIO) + center.y,
  };

  // Disegno della lancetta bianca con bordo nero per visibilità
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 8);
  graphics_draw_line(ctx, center, needle_end);
  
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, center, needle_end);

  // Perno centrale della lancetta
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 5);
}

static void graph_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (!layer) return;
  if (s_history_count < 2) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "Loading data...", fonts_get_system_font(FONT_KEY_GOTHIC_18), 
                       GRect(0, bounds.size.h/2 - 10, bounds.size.w, 30), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // Limite massimo di punti per l'array allocato (40)
  int actual_history_count = s_history_count;
  if (actual_history_count > 40) actual_history_count = 40;
  
  int min_val = s_history_visual_min;
  int max_val = s_history_visual_max;
  if (max_val == min_val) { max_val += 10; } 

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);

  int graph_padding_y = 5; // Piccolo padding interno al layer del grafico
  int graph_h = bounds.size.h - (graph_padding_y * 2);

  for (int i = 0; i < actual_history_count - 1; i++) {
    int x1 = (i * bounds.size.w) / (actual_history_count - 1);
    int y1 = bounds.size.h - graph_padding_y - ((s_history_data[i] - min_val) * graph_h) / (max_val - min_val);
    int x2 = ((i + 1) * bounds.size.w) / (actual_history_count - 1);
    int y2 = bounds.size.h - graph_padding_y - ((s_history_data[i+1] - min_val) * graph_h) / (max_val - min_val);
    graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));
  }

  // Disegna linee verticali per indicare le ore (5 linee per dividere le 6 ore)
  int num_lines = s_history_duration_hours;
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorLightGray));
  for (int i = 1; i < num_lines; i++) {
    int x = (i * bounds.size.w) / num_lines;
    graphics_draw_line(ctx, GPoint(x, 0), GPoint(x, bounds.size.h));
  }
}

static void request_history_data() {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int32(iter, KEY_GET_HISTORY, s_history_duration_hours); 
    app_message_outbox_send();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Graph: Requesting last %d hours", s_history_duration_hours);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Graph: Failed to start outbox for history request");
  }
}

static void graph_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_history_duration_hours += 6; // Aumenta durata
  request_history_data();
}

static void graph_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_history_duration_hours > 6) { 
    s_history_duration_hours -= 6;
    request_history_data();
  }
}

static void graph_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, graph_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, graph_down_click_handler);
}

static void graph_window_load(Window *window) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Graph: Loading window...");
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // Layer Titolo e Max (in alto, 2 righe)
  s_graph_title_layer = text_layer_create(GRect(0, 0, window_bounds.size.w, 55));
  if (s_graph_title_layer) {
    text_layer_set_text(s_graph_title_layer, "Loading...");
    text_layer_set_text_alignment(s_graph_title_layer, GTextAlignmentCenter);
    text_layer_set_font(s_graph_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    layer_add_child(window_layer, text_layer_get_layer(s_graph_title_layer));
  }

  // Layer Min (in basso, 1 riga)
  s_graph_min_layer = text_layer_create(GRect(0, window_bounds.size.h - 30, window_bounds.size.w, 30));
  if (s_graph_min_layer) {
    text_layer_set_text(s_graph_min_layer, "Loading..."); // Consistent initial text
    text_layer_set_text_alignment(s_graph_min_layer, GTextAlignmentCenter);
    text_layer_set_font(s_graph_min_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    layer_add_child(window_layer, text_layer_get_layer(s_graph_min_layer));
  }

  // Layer del grafico (compreso tra i due layer di testo, altezza adattata)
  s_graph_layer = layer_create(GRect(0, 55, window_bounds.size.w, window_bounds.size.h - 55 - 30)); 
  if (s_graph_layer) {
    layer_set_update_proc(s_graph_layer, graph_update_proc);
    layer_add_child(window_layer, s_graph_layer);
  }

  window_set_click_config_provider(window, graph_click_config_provider);
  request_history_data(); // Richiedi i dati con l'offset iniziale
}

static void graph_window_unload(Window *window) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Graph: Unloading window...");
  if (s_graph_title_layer) {
    text_layer_destroy(s_graph_title_layer);
    s_graph_title_layer = NULL;
  }
  if (s_graph_min_layer) {
    text_layer_destroy(s_graph_min_layer);
    s_graph_min_layer = NULL;
  }
  if (s_graph_layer) {
    layer_destroy(s_graph_layer);
    s_graph_layer = NULL;
  }
  s_history_duration_hours = 6; // Reset durata all'uscita
  reset_auto_close_timer();
}

static void update_sensor_display() {
  // Implementazione manuale di parsing e arrotondamento per evitare atof()
  // che causa errori di linker su Pebble SDK
  int integer_part = 0;
  int decimal_digit = 0;
  char *unit = "";
  
  char *p = s_sensor_value_str;
  // Salta eventuali spazi iniziali
  while (*p == ' ') p++;

  // Estrae la parte intera
  while (*p >= '0' && *p <= '9') {
    integer_part = (integer_part * 10) + (*p - '0');
    p++;
  }

  // Se c'è un punto o una virgola, guarda la prima cifra decimale per arrotondare
  if (*p == '.' || *p == ',') {
    p++;
    if (*p >= '0' && *p <= '9') {
      decimal_digit = *p - '0';
      p++;
    }
    // Salta eventuali altri decimali
    while (*p >= '0' && *p <= '9') p++;
  }

  // Arrotondamento (se decimale >= 5, aggiungi 1)
  int rounded_val = (decimal_digit >= 5) ? integer_part + 1 : integer_part;

  // Il resto della stringa è l'unità di misura
  while (*p == ' ') p++; // Salta spazi tra numero e unità
  unit = p;

  // Ricostruiamo la stringa senza decimali per il display
  static char s_rounded_val_str[48];
  if (unit && *unit != '\0') {
    snprintf(s_rounded_val_str, sizeof(s_rounded_val_str), "%d %s", rounded_val, unit);
  } else {
    snprintf(s_rounded_val_str, sizeof(s_rounded_val_str), "%d", rounded_val);
  }

  text_layer_set_text(s_countdown_layer, s_rounded_val_str);
  text_layer_set_text(s_sensor_name_layer, s_sensor_name_str);
  
  // Calcola la percentuale target
  int new_percent = 0;
  // Se l'unità contiene 'W' (Watt) o il valore supera 100, usiamo la scala 0-4000
  if (rounded_val > 100 || strstr(unit, "W") != NULL) {
    new_percent = (rounded_val * 100) / 4000;
  } else {
    new_percent = rounded_val;
  }
  if (new_percent < 0) new_percent = 0;
  if (new_percent > 100) new_percent = 100;

  // Aggiornamento diretto senza animazione per risparmiare RAM
  s_sensor_percent = new_percent;
  s_target_percent = new_percent;
  if (s_chart_layer) layer_mark_dirty(s_chart_layer);
}

static void trigger_light_toggle(int row) {
  if (row < 0 || row >= s_num_lights) return;

  int real_index = s_display_order[row];
  // Invia comando toggle a JS
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_cstring(iter, KEY_TOGGLE_LIGHT, s_lights[real_index].entity);
    app_message_outbox_send();
  }

  // Feedback tattile
  vibes_short_pulse();
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  reset_auto_close_timer();
  trigger_light_toggle(cell_index->row);
}

static void update_menu() {
  if(s_menu_layer) {
    // Inizializza ordine di default
    for(int i=0; i<s_num_lights; i++) s_display_order[i] = i;

    menu_layer_reload_data(s_menu_layer);
  }
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *sensor_tuple = dict_find(iter, KEY_SENSOR_VALUE);
  Tuple *name_tuple = dict_find(iter, KEY_SENSOR_NAME);
  Tuple *idx_tuple = dict_find(iter, KEY_LIGHT_INDEX);
  Tuple *unit_tuple = dict_find(iter, KEY_SENSOR_UNIT);
  Tuple *chat_tuple = dict_find(iter, KEY_CHAT_TEXT);
  Tuple *nick_tuple = dict_find(iter, KEY_NICKNAME);
  Tuple *music_title_tuple = dict_find(iter, KEY_MUSIC_TITLE);
  Tuple *config_open_tuple = dict_find(iter, KEY_CONFIG_OPEN);
  Tuple *history_tuple = dict_find(iter, KEY_HISTORY_DATA);
  Tuple *global_max_tuple = dict_find(iter, KEY_GLOBAL_MAX); // Nuovo
  Tuple *global_min_tuple = dict_find(iter, KEY_GLOBAL_MIN); // Nuovo
  
  if (nick_tuple) {
    snprintf(s_pebble_nickname, sizeof(s_pebble_nickname), "%s", nick_tuple->value->cstring);
  }

  if (unit_tuple) {
    snprintf(s_unit_str, sizeof(s_unit_str), "%s", unit_tuple->value->cstring);
  }

  if (name_tuple) {
    snprintf(s_sensor_name_str, sizeof(s_sensor_name_str), "%s", name_tuple->value->cstring);
  }

  if (sensor_tuple) {
    // Riceviamo il valore del sensore selezionato
    snprintf(s_sensor_value_str, sizeof(s_sensor_value_str), "%s", sensor_tuple->value->cstring);
    update_sensor_display();
  }

  if (idx_tuple) {
    int idx = idx_tuple->value->int32;
    if (idx < 10) {
      Tuple *name_tuple = dict_find(iter, KEY_LIGHT_NAME);
      Tuple *state_tuple = dict_find(iter, KEY_LIGHT_STATE);
      Tuple *entity_tuple = dict_find(iter, KEY_LIGHT_ENTITY);
      
      if (name_tuple) snprintf(s_lights[idx].name, sizeof(s_lights[idx].name), "%s", name_tuple->value->cstring);
      if (state_tuple) snprintf(s_lights[idx].state, sizeof(s_lights[idx].state), "%s", state_tuple->value->cstring);
      if (entity_tuple) snprintf(s_lights[idx].entity, sizeof(s_lights[idx].entity), "%s", entity_tuple->value->cstring);
      
      if (idx >= s_num_lights) s_num_lights = idx + 1;
      update_menu();
    }
  }

  if (chat_tuple) {
    add_chat_message(chat_tuple->value->cstring);
  }

  if (music_title_tuple) {
    snprintf(s_music_title_str, sizeof(s_music_title_str), "%s", music_title_tuple->value->cstring);
    if (s_music_title_layer) {
      text_layer_set_text(s_music_title_layer, s_music_title_str);
    }
  }

  if (config_open_tuple) {
    if (config_open_tuple->value->int32 == 1) {
      if (s_auto_close_timer) {
        app_timer_cancel(s_auto_close_timer);
        s_auto_close_timer = NULL;
      }
    } else {
      reset_auto_close_timer();
    }
  }

  if (history_tuple) {
    char *str = history_tuple->value->cstring;
    s_history_count = 0;
    char *p = str;
    while (p && *p != '\0' && s_history_count < 40) {
      s_history_data[s_history_count++] = atoi(p);
      p = strchr(p, ',');
      if (p) p++;
    }

    char time_range_str[32];
    snprintf(time_range_str, sizeof(time_range_str), "LAST %d HOURS", s_history_duration_hours);

    // Variabili per i valori da visualizzare
    int display_min_val = 0;
    int display_max_val = 0;
    bool has_data_to_display = false;

    if (global_max_tuple && global_min_tuple) {
      display_max_val = global_max_tuple->value->int32;
      display_min_val = global_min_tuple->value->int32;
      has_data_to_display = true;
    }

    // Aggiorna le variabili globali per la scala del grafico
    if (has_data_to_display) {
      s_history_visual_min = display_min_val;
      s_history_visual_max = display_max_val;
    }

    if (has_data_to_display) {
      snprintf(s_graph_max_str, sizeof(s_graph_max_str), "%s\nMax: %s%d.%d %s",
               time_range_str, (display_max_val < 0 && display_max_val > -10) ? "-" : "",
               display_max_val / 10, abs(display_max_val % 10), s_unit_str);
      snprintf(s_graph_min_str, sizeof(s_graph_min_str), "Min: %s%d.%d %s",
               (display_min_val < 0 && display_min_val > -10) ? "-" : "",
               display_min_val / 10, abs(display_min_val % 10), s_unit_str);
    } else {
      snprintf(s_graph_max_str, sizeof(s_graph_max_str), "%s\nNo data", time_range_str);
      snprintf(s_graph_min_str, sizeof(s_graph_min_str), "No data");
    }

    if (s_graph_title_layer) text_layer_set_text(s_graph_title_layer, s_graph_max_str);
    if (s_graph_min_layer) text_layer_set_text(s_graph_min_layer, s_graph_min_str);
    if (s_graph_layer) layer_mark_dirty(s_graph_layer); // Forza il ridisegno del grafico
  }
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_num_lights;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, "Menu Hassio");
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int index = s_display_order[cell_index->row];
  bool is_on = (strcmp(s_lights[index].state, "on") == 0);
  if (menu_cell_layer_is_highlighted(cell_layer)) {
    #if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, GColorCobaltBlue);
    #else
    graphics_context_set_fill_color(ctx, GColorBlack);
    #endif
  } else {
    #if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, is_on ? GColorChromeYellow : GColorWhite);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
  }
  graphics_fill_rect(ctx, layer_get_bounds(cell_layer), 0, GCornerNone);

  menu_cell_basic_draw(ctx, cell_layer, s_lights[index].name, s_lights[index].state, PBL_IF_ROUND_ELSE(NULL, NULL));
}

static void menu_scroll_up_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  menu_layer_set_selected_next(s_menu_layer, true, MenuRowAlignCenter, true);
}

static void menu_scroll_down_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  menu_layer_set_selected_next(s_menu_layer, false, MenuRowAlignCenter, true);
}

static void menu_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  MenuIndex index = menu_layer_get_selected_index(s_menu_layer);
  menu_select_callback(s_menu_layer, &index, NULL);
}

static void menu_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_select_click_handler);
  // Custom handlers for scrolling to reset timer
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 50, menu_scroll_up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 50, menu_scroll_down_handler);
}

static void prv_menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_get_num_rows_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
  });
  window_set_click_config_provider(window, menu_click_provider);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
  
  update_menu();
}

static void prv_menu_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

static void reset_auto_close_timer() {
  if (s_auto_close_timer) {
    app_timer_reschedule(s_auto_close_timer, 10000);
  } else {
    s_auto_close_timer = app_timer_register(10000, auto_close_callback, NULL);
  }
}

static void auto_close_callback(void *data) {
  s_auto_close_timer = NULL;
  window_stack_pop_all(true);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  if (!s_music_window) {
    s_music_window = window_create();
    window_set_window_handlers(s_music_window, (WindowHandlers) {
      .load = music_window_load,
      .unload = music_window_unload,
    });
  }
  window_stack_push(s_music_window, true);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  if (!s_menu_window) {
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
      .load = prv_menu_window_load,
      .unload = prv_menu_window_unload,
    });
  }
  window_stack_push(s_menu_window, true);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  reset_auto_close_timer();
  if (!s_chat_window) {
    s_chat_window = window_create();
    window_set_window_handlers(s_chat_window, (WindowHandlers) {
      .load = chat_window_load,
      .unload = chat_window_unload,
    });
  }
  window_stack_push(s_chat_window, true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Imposta sfondo bianco
  window_set_background_color(window, GColorWhite);

  // Crea il layer per il Gauge (In alto, occupa la metà superiore)
  s_chart_layer = layer_create(GRect(0, 0, bounds.size.w, bounds.size.w));
  layer_set_update_proc(s_chart_layer, chart_update_proc);
  layer_add_child(window_layer, s_chart_layer);

  // Layer Sensore (Sotto il grafico, al centro)
  s_countdown_layer = text_layer_create(GRect(0, bounds.size.h / 2, bounds.size.w, 40));
  text_layer_set_background_color(s_countdown_layer, GColorClear);
  text_layer_set_text_color(s_countdown_layer, GColorBlack);
  text_layer_set_font(s_countdown_layer, fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_28_BOLD, FONT_KEY_BITHAM_30_BLACK)));
  text_layer_set_text_alignment(s_countdown_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_countdown_layer));
  
  // Layer Nome Sensore (In basso)
  s_sensor_name_layer = text_layer_create(GRect(0, (bounds.size.h / 2) + 40, bounds.size.w, 20));
  text_layer_set_background_color(s_sensor_name_layer, GColorClear);
  text_layer_set_text_color(s_sensor_name_layer, GColorBlack);
  text_layer_set_font(s_sensor_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_sensor_name_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_sensor_name_layer));

  // Abilita i click per aprire il menu
  window_set_click_config_provider(window, click_config_provider);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_countdown_layer);
  s_countdown_layer = NULL;
  text_layer_destroy(s_sensor_name_layer);
  s_sensor_name_layer = NULL;
  layer_destroy(s_chart_layer);
  s_chart_layer = NULL;
  if (s_feedback_timer) {
    app_timer_cancel(s_feedback_timer);
    s_feedback_timer = NULL;
  }
  if (s_auto_close_timer) {
    app_timer_cancel(s_auto_close_timer);
    s_auto_close_timer = NULL;
  }
}

static bool s_can_shake = true;

static void shake_cooldown_callback(void *data) {
  s_can_shake = true;
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_can_shake) return;

  Window *top_window = window_stack_get_top_window();
  
  // Se il grafico è già aperto o in apertura, ignora lo shake
  if (top_window == s_graph_window) return;

  if (s_menu_window && top_window == s_menu_window && s_menu_layer) {
    reset_auto_close_timer();
    s_can_shake = false;
    app_timer_register(1000, shake_cooldown_callback, NULL);

    MenuIndex selected = menu_layer_get_selected_index(s_menu_layer);
    trigger_light_toggle(selected.row);
  } else if (top_window == s_window) {
    s_can_shake = false;
    app_timer_register(1000, shake_cooldown_callback, NULL);
    s_history_count = 0; // Reset dati precedenti

    // Blocca IMMEDIATAMENTE il timeout di chiusura automatica globale
    if (s_auto_close_timer) {
      app_timer_cancel(s_auto_close_timer);
      s_auto_close_timer = NULL;
    }

    if (!s_graph_window) {
      s_graph_window = window_create();
      window_set_window_handlers(s_graph_window, (WindowHandlers) {
        .load = graph_window_load,
        .unload = graph_window_unload,
      });
      if (!s_graph_window) { // Verifica se la creazione della finestra è fallita
        APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create graph window!");
        return; // Interrompi se la creazione della finestra non è riuscita
      }
    }
    window_stack_push(s_graph_window, true);
  }
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox sent successfully");
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  const bool animated = true;
  window_stack_push(s_window, animated);

  // Registra AppMessage per ricevere la configurazione
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(1024, 1024); // Aumentato ulteriormente per evitare troncamenti su range ampi

  // Iscrizione al tap per il toggle
  accel_tap_service_subscribe(accel_tap_handler);

  // Chiudi automaticamente dopo 10 secondi
  s_auto_close_timer = app_timer_register(10000, auto_close_callback, NULL);
}

static void prv_deinit(void) {
  window_destroy(s_window);
  if (s_menu_window) window_destroy(s_menu_window);
  if (s_chat_window) window_destroy(s_chat_window);
  if (s_canned_window) window_destroy(s_canned_window);
  if (s_music_window) window_destroy(s_music_window);
  if (s_graph_window) window_destroy(s_graph_window);
  accel_tap_service_unsubscribe();
}

int main(void) {
  prv_init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", s_window);

  app_event_loop();
  prv_deinit();
}
