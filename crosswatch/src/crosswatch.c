#include <pebble.h>

#define COLORS PBL_IF_COLOR_ELSE(true, false)
#define ANTIALIASING true

#define SAFEMODE_ON 0
#define SAFEMODE_OFF 6

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *s_main_window;
static Layer *s_canvas_layer;
static TextLayer *s_weather_layer;
static TextLayer *s_date_layer;
static TextLayer *s_hour_layer;
static TextLayer *s_minute_layer;

static GFont s_weather_font;
static GFont s_time_font;

static GPoint s_center;
static char s_last_hour[8];
static char s_last_minute[8];
static char s_last_date[16];
static int background_color;

static bool weather_units_conf = false;
static bool weather_safemode_conf = true;
static bool weather_on_conf = false;
static bool background_on_conf = false;

/*************************** appMessage **************************/

static void inbox_received_callback(DictionaryIterator *iterator,
                                    void *context) {
  // Store incoming information
  static char icon_buffer[16];
  static char temperature_buffer[8];
  static int temperature;

  // Read tuples for data
  Tuple *weather_units_tuple = dict_find(iterator, MESSAGE_KEY_UNITS);
  Tuple *weather_on_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_ON);
  Tuple *weather_safemode_tuple =
      dict_find(iterator, MESSAGE_KEY_WEATHER_SAFEMODE);
  Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  Tuple *icon_tuple = dict_find(iterator, MESSAGE_KEY_ICON);
  Tuple *background_color_tuple =
      dict_find(iterator, MESSAGE_KEY_BACKGROUND_COLOR);
  Tuple *background_on_tuple = dict_find(iterator, MESSAGE_KEY_BACKGROUND_ON);

  // If we get weather option
  if (weather_on_tuple) {
    // Set weather flag
    weather_on_conf = (bool)weather_on_tuple->value->int16;
    persist_write_bool(MESSAGE_KEY_WEATHER_ON, weather_on_conf);
  }

  if (weather_safemode_tuple) {
    weather_safemode_conf = (bool)weather_safemode_tuple->value->int16;
    persist_write_bool(MESSAGE_KEY_WEATHER_SAFEMODE, weather_safemode_conf);
  }

  if (weather_units_tuple) {
    weather_units_conf = (bool)weather_units_tuple->value->int16;
    persist_write_bool(MESSAGE_KEY_UNITS, weather_units_conf);
  }

  // If all data is available, use it
  if (temp_tuple && icon_tuple) {
    // Assemble strings for temp and icon
    temperature = (float)temp_tuple->value->int32;

    if (weather_units_conf) {
      snprintf(temperature_buffer, sizeof(temperature_buffer), "%d F",
               temperature);
    } else {
      snprintf(temperature_buffer, sizeof(temperature_buffer), "%d C",
               temperature);
    }

    snprintf(icon_buffer, sizeof(icon_buffer), "%s %s",
             icon_tuple->value->cstring, temperature_buffer);

    // Set temp and icon to text layers
    text_layer_set_text(s_weather_layer, icon_buffer);
  }

  // If weather disabled, clear weather layers
  if (!weather_on_conf) {
    text_layer_set_text(s_weather_layer, "");
  }

  // If background color and enabled
  if (background_color_tuple && background_on_tuple) {
    // Set background on/off
    background_on_conf = (bool)background_on_tuple->value->int16;
    persist_write_bool(MESSAGE_KEY_BACKGROUND_ON, background_on_conf);
    // Set background color if enabled, otherwise we load the default one - red
    background_color = background_on_conf
                           ? (int)background_color_tuple->value->int32
                           : 0xFF0000;
    persist_write_int(MESSAGE_KEY_BACKGROUND_COLOR, background_color);

    // Redraw
    if (s_canvas_layer) {
      layer_mark_dirty(s_canvas_layer);
    }
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "weather_units_conf %d", weather_units_conf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "weather_on_conf %d", weather_on_conf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "background_on_conf %d", background_on_conf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "background_color %d", background_color);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator,
                                   AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

/************************************ UI **************************************/

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  static bool in_interval = true;

  strftime(s_last_date, sizeof(s_last_date), "%a %d", tick_time);
  strftime(s_last_hour, sizeof(s_last_hour),
           clock_is_24h_style() ? "%H" : "%I:%M", tick_time); // :%M
  strftime(s_last_minute, sizeof(s_last_minute), "%M", tick_time);

  if (weather_safemode_conf) {
    if (tick_time->tm_hour >= SAFEMODE_ON &&
        tick_time->tm_hour <= SAFEMODE_OFF) {
      in_interval = false;
      APP_LOG(APP_LOG_LEVEL_INFO, "in_interval");
    }
  }

  // Get weather update every 30 minutes
  if (tick_time->tm_min % 30 == 0 && weather_on_conf && in_interval) {
    // Begin dictionary
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    // Add a key-value pair
    dict_write_uint8(iter, 0, 0);

    // Send the message!
    app_message_outbox_send();
  }

  // Redraw
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static void update_proc(Layer *layer, GContext *ctx) {
  // Color background?
  GRect bounds = layer_get_bounds(layer);
  GRect block1 = GRect(0, 0, bounds.size.w, 50);
  GRect block2 = GRect(0, 50, bounds.size.w, 50);
  GRect divider = GRect(bounds.size.w / 2 - 4, 146, 6, 6);
  GRect battery_bg = GRect(bounds.size.w / 3, 148, bounds.size.w / 3, 50);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // Set date
  text_layer_set_text(s_date_layer, s_last_date);

  // Set time
  text_layer_set_text(s_hour_layer, s_last_hour);
  text_layer_set_text(s_minute_layer, s_last_minute);

  // White clockface
  graphics_context_set_fill_color(ctx, GColorWhite);

  // Create first block
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, block1, 0, GCornerNone);
  graphics_draw_rect(ctx, block1);

  // Create second block
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, block2, 0, GCornerNone);
  graphics_draw_rect(ctx, block2);

  // Create time divider
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, divider, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, divider);

  for (int i = 0; i < bounds.size.w; i++) {
    if (i % 4 == 0) {
      GRect pixel = GRect(i, 49, 2, 2);

      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, pixel, 0, GCornerNone);
      graphics_draw_rect(ctx, pixel);

      GRect pixel2 = GRect(i, 100, 2, 2);

      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_fill_rect(ctx, pixel2, 0, GCornerNone);
      graphics_draw_rect(ctx, pixel2);
    }
  }
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  s_center = grect_center_point(&window_bounds);

  s_canvas_layer = layer_create(window_bounds);

  layer_set_update_proc(s_canvas_layer, update_proc);

  layer_add_child(window_layer, s_canvas_layer);

  // Create time Layer
  s_hour_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(100, 100), window_bounds.size.w / 2 - 5, 55));
  s_minute_layer = text_layer_create(GRect(window_bounds.size.w / 2 + 10,
                                           PBL_IF_ROUND_ELSE(100, 100),
                                           window_bounds.size.w / 2 - 5, 55));

  // Style the time text
  text_layer_set_background_color(s_hour_layer, GColorClear);
  text_layer_set_text_color(s_hour_layer, GColorBlack);
  text_layer_set_text_alignment(s_hour_layer, GTextAlignmentRight);

  text_layer_set_background_color(s_minute_layer, GColorClear);
  text_layer_set_text_color(s_minute_layer, GColorBlack);
  text_layer_set_text_alignment(s_minute_layer, GTextAlignmentLeft);

  // Create date Layer
  s_date_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(10, 10), window_bounds.size.w, 28));

  // Style the date text
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);

  // Create weather icon Layer
  s_weather_layer = text_layer_create(
      GRect(0, PBL_IF_ROUND_ELSE(60, 60), window_bounds.size.w, 28));

  // Style the icon
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, GColorBlack);
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);

  // Set fonts
  s_time_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_PIXEL_MIL_52));
  s_weather_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_PIXEL_MIL_24));

  text_layer_set_font(s_hour_layer, s_time_font);
  text_layer_set_font(s_minute_layer, s_time_font);
  text_layer_set_font(s_date_layer, s_weather_font);
  text_layer_set_font(s_weather_layer, s_weather_font);

  // Add layers
  layer_add_child(window_get_root_layer(window),
                  text_layer_get_layer(s_hour_layer));
  layer_add_child(window_get_root_layer(window),
                  text_layer_get_layer(s_minute_layer));
  layer_add_child(window_get_root_layer(window),
                  text_layer_get_layer(s_date_layer));
  layer_add_child(window_get_root_layer(window),
                  text_layer_get_layer(s_weather_layer));
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
  // Destroy weather elements
  text_layer_destroy(s_hour_layer);
  text_layer_destroy(s_minute_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weather_layer);
  fonts_unload_custom_font(s_weather_font);
  fonts_unload_custom_font(s_time_font);
}

/*********************************** App **************************************/

static void init() {
  srand(time(NULL));

  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  tick_handler(time_now, MINUTE_UNIT);

  s_main_window = window_create();

  weather_on_conf = persist_exists(MESSAGE_KEY_WEATHER_ON)
                        ? persist_read_bool(MESSAGE_KEY_WEATHER_ON)
                        : false;
  weather_safemode_conf = persist_exists(MESSAGE_KEY_WEATHER_SAFEMODE)
                              ? persist_read_bool(MESSAGE_KEY_WEATHER_SAFEMODE)
                              : true;
  weather_units_conf = persist_exists(MESSAGE_KEY_UNITS)
                           ? persist_read_bool(MESSAGE_KEY_UNITS)
                           : false;
  background_on_conf = persist_exists(MESSAGE_KEY_BACKGROUND_ON)
                           ? persist_read_bool(MESSAGE_KEY_BACKGROUND_ON)
                           : false;
  background_color = persist_exists(MESSAGE_KEY_BACKGROUND_COLOR)
                         ? persist_read_int(MESSAGE_KEY_BACKGROUND_COLOR)
                         : 0xFF0000;

  window_set_window_handlers(s_main_window,
                             (WindowHandlers){
                                 .load = window_load, .unload = window_unload,
                             });

  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // Register callbacks
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());
}

static void deinit() { window_destroy(s_main_window); }

int main() {
  init();
  app_event_loop();
  deinit();
}