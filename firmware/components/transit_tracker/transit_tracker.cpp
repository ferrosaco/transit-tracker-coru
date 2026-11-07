#include "transit_tracker.h"
#include "string_utils.h"

#include <cstring>
#include <cstdlib>
#include <set>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace transit_tracker {

static const char *const TAG = "transit_tracker.component";

static constexpr unsigned long POLL_INTERVAL_MS = 15000;
static constexpr unsigned long CATALOG_TTL_MS = 6UL * 60UL * 60UL * 1000UL;
static constexpr size_t MAX_HTTP_BODY = 200000;
static const char *const STATIC_URL =
    "https://itranvias.com/queryitr_v3.php?func=7&dato=20160101T000000_es_0_20160101T000000";
static const char *const LIVE_URL_PREFIX = "https://itranvias.com/queryitr_v3.php?func=0&dato=";

static bool parse_hex_color(const std::string &str, uint32_t &out) {
  if (str.empty()) {
    return false;
  }
  char *end = nullptr;
  unsigned long value = std::strtoul(str.c_str(), &end, 16);
  if (end == str.c_str() || *end != '\0') {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

static std::string json_text(JsonVariant value) {
  if (value.is<const char *>()) {
    const char *text = value.as<const char *>();
    return text == nullptr ? std::string() : std::string(text);
  }
  if (value.is<int>()) {
    return std::to_string(value.as<int>());
  }
  if (value.is<long>()) {
    return std::to_string(value.as<long>());
  }
  return "";
}

struct PsramBody {
  char *data{nullptr};
  size_t size{0};
  ~PsramBody() { heap_caps_free(this->data); }
  PsramBody() = default;
  PsramBody(const PsramBody &) = delete;
  PsramBody &operator=(const PsramBody &) = delete;
};

static bool fetch_url(const char *url, PsramBody &body) {
  heap_caps_free(body.data);
  body.data = nullptr;
  body.size = 0;
  char *buffer = static_cast<char *>(heap_caps_malloc(MAX_HTTP_BODY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    ESP_LOGW(TAG, "No PSRAM for the iTranvías response");
    return false;
  }
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = "TransitTrackerCoruna/1.0";
  config.buffer_size = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    heap_caps_free(buffer);
    ESP_LOGW(TAG, "HTTP client init failed");
    return false;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    heap_caps_free(buffer);
    ESP_LOGW(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    heap_caps_free(buffer);
    ESP_LOGW(TAG, "HTTP %d for %s", status, url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  char chunk[1024];
  int idle_reads = 0;
  size_t size = 0;
  while (size < MAX_HTTP_BODY) {
    App.feed_wdt();
    int read = esp_http_client_read(client, chunk, sizeof(chunk));
    if (read < 0) {
      ESP_LOGW(TAG, "HTTP read failed");
      heap_caps_free(buffer);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    if (read == 0) {
      if (esp_http_client_is_complete_data_received(client) || ++idle_reads > 3) {
        break;
      }
      continue;
    }
    idle_reads = 0;
    memcpy(buffer + size, chunk, static_cast<size_t>(read));
    size += static_cast<size_t>(read);
  }

  buffer[size] = '\0';
  body.data = buffer;
  body.size = size;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return size > 0;
}

void TransitTracker::setup() {
  // TLS plus the full certificate bundle needs more stack than the main loop
  // has. The stack itself lives in PSRAM so it does not eat the internal RAM
  // the Wi-Fi and the matrix need.
  static constexpr uint32_t stack_bytes = 48 * 1024;
  this->poll_stack_ =
      static_cast<StackType_t *>(heap_caps_malloc(stack_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->poll_stack_ != nullptr) {
    this->poll_task_handle_ = xTaskCreateStaticPinnedToCore(&TransitTracker::poll_task_thunk_, "itranvias", stack_bytes,
                                                            this, 1, this->poll_stack_, &this->poll_tcb_, tskNO_AFFINITY);
  }
  if (this->poll_task_handle_ == nullptr) {
    ESP_LOGE(TAG, "iTranvías task was not created");
  } else {
    ESP_LOGI(TAG, "iTranvías task started");
  }
  this->set_interval("poll_itranvias", POLL_INTERVAL_MS, [this]() { this->request_refresh_(); });
  this->set_timeout("poll_itranvias_now", 1000, [this]() { this->request_refresh_(); });
}

void TransitTracker::loop() {
  this->apply_result_();
  if (this->fully_closed_.load() || this->poll_busy_.load() || !this->refresh_requested_.load()) {
    return;
  }
  if (!esphome::network::is_connected()) {
    return;
  }
  auto now = this->rtc_->now();
  if (!now.is_valid()) {
    return;
  }

  PollJob job = this->make_job_(now.timestamp);
  if (this->poll_task_handle_ == nullptr) {
    this->refresh_requested_.store(false);
    FetchResult failed;
    failed.generation = job.generation;
    failed.valid_pairs = true;
    failed.failure = FetchFailure::CATALOG;
    this->apply_result_now_(std::move(failed));
    return;
  }

  this->job_ = std::move(job);
  this->refresh_requested_.store(false);
  this->poll_busy_.store(true);
  xTaskNotifyGive(this->poll_task_handle_);
}

void TransitTracker::poll_task_thunk_(void *param) { static_cast<TransitTracker *>(param)->poll_task_(); }

void TransitTracker::poll_task_() {
  while (!this->fully_closed_.load()) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    if (this->fully_closed_.load()) {
      break;
    }
    if (!this->poll_busy_.load()) {
      continue;
    }
    PollJob job = this->job_;
    FetchResult result = this->fetch_schedule_(job);
    {
      std::lock_guard<std::mutex> lock(this->result_mutex_);
      this->result_ = std::move(result);
      this->result_ready_ = true;
    }
    this->poll_busy_.store(false);
  }
  vTaskDelete(nullptr);
}

void TransitTracker::dump_config() {
  ESP_LOGCONFIG(TAG, "Transit Tracker:");
  ESP_LOGCONFIG(TAG, "  Source: iTranvías queryitr");
  ESP_LOGCONFIG(TAG, "  Schedule: %s", this->schedule_string_.c_str());
  ESP_LOGCONFIG(TAG, "  Limit: %d", this->limit_);
  ESP_LOGCONFIG(TAG, "  List mode: %s", this->list_mode_.c_str());
  ESP_LOGCONFIG(TAG, "  Display departure times: %s", this->display_departure_times_ ? "true" : "false");
  ESP_LOGCONFIG(TAG, "  Scroll Headsigns: %s", this->scroll_headsigns_ ? "true" : "false");
}

void TransitTracker::request_refresh_() {
  if (!this->fully_closed_) {
    this->refresh_requested_ = true;
  }
}

void TransitTracker::set_schedule_string(const std::string &schedule_string) {
  std::vector<itranvias::RouteStopPair> pairs;
  itranvias::parse_route_stop_pairs(schedule_string, pairs);
  bool valid = !pairs.empty();
  if (schedule_string == this->schedule_string_ && valid == this->has_valid_pairs_) {
    return;
  }
  this->generation_.fetch_add(1);
  this->schedule_string_ = schedule_string;
  this->has_valid_pairs_ = valid;
  this->has_loaded_ = false;
  if (!valid) {
    std::lock_guard<std::mutex> lock(this->schedule_state_.mutex);
    this->schedule_state_.trips.clear();
    this->status_clear_error();
  }
  this->request_refresh_();
}

void TransitTracker::set_list_mode(const std::string &list_mode) {
  if (list_mode == this->list_mode_) {
    return;
  }
  this->list_mode_ = list_mode;
  this->generation_.fetch_add(1);
  this->request_refresh_();
}

void TransitTracker::reconnect(const char *reason) {
  if (this->fully_closed_) {
    return;
  }
  ESP_LOGI(TAG, "Refreshing iTranvías (reason: %s)", reason);
  this->request_refresh_();
}

void TransitTracker::close(bool fully) {
  if (fully) {
    this->fully_closed_ = true;
  }
}

void TransitTracker::on_shutdown() {
  this->cancel_interval("poll_itranvias");
  this->close(true);
  if (this->poll_task_handle_ != nullptr) {
    xTaskNotifyGive(this->poll_task_handle_);
  }
}

bool TransitTracker::ingest_network_(const char *body, size_t length) {
  JsonDocument doc = json::parse_json(reinterpret_cast<const uint8_t *>(body), length);
  if (doc.isNull()) {
    ESP_LOGW(TAG, "Static catalog is not valid JSON (%u bytes)", static_cast<unsigned>(length));
    return false;
  }

  JsonObject update = doc["iTranvias"]["actualizacion"];
  if (update.isNull()) {
    ESP_LOGW(TAG, "Static catalog has no actualizacion");
    return false;
  }

  itranvias::Network network;
  for (JsonObject line : update["lineas"].as<JsonArray>()) {
    itranvias::Line parsed;
    parsed.id = line["id"].as<int>();
    parsed.name = json_text(line["lin_comer"]);
    if (parsed.name.empty()) {
      parsed.name = std::to_string(parsed.id);
    }
    parsed.origin = json_text(line["nombre_orig"]);
    parsed.destination = json_text(line["nombre_dest"]);
    parsed.color = json_text(line["color"]);
    if (!parsed.color.empty() && parsed.color[0] == '#') {
      parsed.color.erase(parsed.color.begin());
    }
    for (JsonObject route : line["rutas"].as<JsonArray>()) {
      std::vector<int> stops;
      for (JsonVariant stop_id : route["paradas"].as<JsonArray>()) {
        stops.push_back(stop_id.as<int>());
      }
      parsed.routes.push_back(std::move(stops));
    }
    network.lines[parsed.id] = std::move(parsed);
  }

  for (JsonObject stop : update["paradas"].as<JsonArray>()) {
    int stop_id = stop["id"].as<int>();
    network.stop_names[stop_id] = json_text(stop["nombre"]);
  }

  if (network.lines.empty() || network.stop_names.empty()) {
    ESP_LOGW(TAG, "Static catalog did not contain lines and stops");
    return false;
  }

  this->network_ = std::move(network);
  ESP_LOGI(TAG, "iTranvías catalog: %u lines, %u stops", static_cast<unsigned>(this->network_.lines.size()),
           static_cast<unsigned>(this->network_.stop_names.size()));
  return true;
}

bool TransitTracker::ingest_buses_(const char *body, size_t length, std::vector<itranvias::LiveBus> &out) {
  out.clear();
  JsonDocument doc = json::parse_json(reinterpret_cast<const uint8_t *>(body), length);
  if (doc.isNull()) {
    return false;
  }
  std::string result = json_text(doc["resultado"]);
  if (result != "OK") {
    ESP_LOGW(TAG, "iTranvías resultado: %s", result.c_str());
    return false;
  }

  for (JsonObject line : doc["buses"]["lineas"].as<JsonArray>()) {
    int line_id = line["linea"].as<int>();
    for (JsonObject bus : line["buses"].as<JsonArray>()) {
      int minutes = 0;
      bool imminent = false;
      if (!itranvias::parse_minutes(json_text(bus["tiempo"]), minutes, imminent)) {
        continue;
      }
      itranvias::LiveBus live;
      live.line_id = line_id;
      live.vehicle = json_text(bus["bus"]);
      live.minutes = minutes;
      live.imminent = imminent;
      live.last_stop_id = bus["ult_parada"].isNull() ? -1 : bus["ult_parada"].as<int>();
      out.push_back(live);
    }
  }
  return true;
}

TransitTracker::PollJob TransitTracker::make_job_(time_t now) const {
  PollJob job;
  job.generation = this->generation_.load();
  job.now = now;
  job.limit = this->limit_;
  job.schedule = this->schedule_string_;
  job.list_mode = this->list_mode_;
  job.abbreviations = this->abbreviations_;
  job.route_styles = this->route_styles_;
  job.default_route_color = this->default_route_color_;
  return job;
}

std::vector<Trip> TransitTracker::trips_from_(const std::vector<itranvias::BuiltTrip> &trips, const PollJob &job) const {
  std::vector<Trip> drawn;
  drawn.reserve(trips.size());
  for (const auto &trip : trips) {
    std::string headsign = trip.headsign;
    for (const auto &abbr : job.abbreviations) {
      size_t pos = headsign.find(abbr.first);
      if (pos != std::string::npos) {
        headsign.replace(pos, abbr.first.length(), abbr.second);
      }
    }

    auto route_style = job.route_styles.find(trip.route_id);
    Color route_color = job.default_route_color;
    std::string route_name = trip.route_name;
    if (route_style != job.route_styles.end()) {
      route_color = route_style->second.color;
      route_name = route_style->second.name;
    } else {
      uint32_t parsed_color;
      if (parse_hex_color(trip.route_color, parsed_color)) {
        route_color = Color(parsed_color);
      }
    }

    drawn.push_back({
        .route_id = trip.route_id,
        .route_name = route_name,
        .route_color = route_color,
        .headsign = headsign,
        .arrival_time = trip.when,
        .departure_time = trip.when,
        .is_realtime = true,
    });
  }
  return drawn;
}

void TransitTracker::apply_result_() {
  FetchResult result;
  {
    std::lock_guard<std::mutex> lock(this->result_mutex_);
    if (!this->result_ready_) {
      return;
    }
    result = std::move(this->result_);
    this->result_ready_ = false;
  }
  this->apply_result_now_(std::move(result));
}

void TransitTracker::apply_result_now_(FetchResult result) {
  if (result.generation != this->generation_.load()) {
    this->request_refresh_();
    return;
  }

  this->has_valid_pairs_ = result.valid_pairs;
  if (!result.valid_pairs) {
    std::lock_guard<std::mutex> lock(this->schedule_state_.mutex);
    this->schedule_state_.trips.clear();
    this->status_clear_error();
    return;
  }

  if (result.failure == FetchFailure::CATALOG) {
    this->last_poll_ok_ = false;
    if (!this->has_loaded_) {
      this->status_set_error(LOG_STR("Failed to load iTranvias catalog"));
    }
    return;
  }
  if (result.failure == FetchFailure::ARRIVALS) {
    this->last_poll_ok_ = false;
    if (!this->has_loaded_) {
      this->status_set_error(LOG_STR("Failed to load iTranvias arrivals"));
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(this->schedule_state_.mutex);
    this->schedule_state_.trips = std::move(result.trips);
  }
  this->has_loaded_ = true;
  this->last_poll_ok_ = true;
  this->status_clear_error();
  ESP_LOGI(TAG, "Published the iTranvías schedule");
}

TransitTracker::FetchResult TransitTracker::fetch_schedule_(const PollJob &job) {
  FetchResult result;
  result.generation = job.generation;
  if (this->fully_closed_.load()) {
    return result;
  }

  std::vector<itranvias::RouteStopPair> pairs;
  itranvias::parse_route_stop_pairs(job.schedule, pairs);
  result.valid_pairs = !pairs.empty();
  if (!result.valid_pairs) {
    return result;
  }

  bool catalog_expired = this->network_loaded_at_ == 0 || millis() - this->network_loaded_at_ > CATALOG_TTL_MS;
  if (!this->network_ready_ || catalog_expired) {
    PsramBody body;
    ESP_LOGI(TAG, "Fetching iTranvías catalog");
    if (!fetch_url(STATIC_URL, body) || !this->ingest_network_(body.data, body.size)) {
      if (!this->network_ready_) {
        result.failure = FetchFailure::CATALOG;
        return result;
      }
      ESP_LOGW(TAG, "Keeping the previous iTranvías catalog");
    } else {
      this->network_ready_ = true;
      this->network_loaded_at_ = millis();
    }
  }

  std::map<int, std::vector<itranvias::LiveBus>> buses_by_stop;
  bool any_failure = false;
  std::set<int> stops;
  for (const auto &pair : pairs) {
    stops.insert(pair.stop_local);
  }
  ESP_LOGI(TAG, "Fetching arrivals for %u stops", static_cast<unsigned>(stops.size()));
  for (int stop_id : stops) {
    std::string url = std::string(LIVE_URL_PREFIX) + std::to_string(stop_id);
    PsramBody body;
    std::vector<itranvias::LiveBus> buses;
    if (!fetch_url(url.c_str(), body) || !this->ingest_buses_(body.data, body.size, buses)) {
      any_failure = true;
      ESP_LOGW(TAG, "No live arrivals for stop %d", stop_id);
      continue;
    }
    buses_by_stop[stop_id] = std::move(buses);
  }

  if (buses_by_stop.empty() && any_failure) {
    result.failure = FetchFailure::ARRIVALS;
    return result;
  }

  auto built = itranvias::build_trips(this->network_, pairs, buses_by_stop, job.now, job.limit, job.list_mode);
  result.trips = this->trips_from_(built, job);
  ESP_LOGD(TAG, "Built %u trips", static_cast<unsigned>(result.trips.size()));
  return result;
}

void TransitTracker::set_abbreviations_from_text(const std::string &text) {
  this->abbreviations_.clear();
  for (const auto &line : split(text, '\n')) {
    auto parts = split(line, ';');

    if (parts.size() == 1) {
      // If only one part is provided, treat it as a removal (replace with empty string)
      this->add_abbreviation(parts[0], "");
      continue;
    }

    if (parts.size() != 2) {
      ESP_LOGW(TAG, "Invalid abbreviation line: %s", line.c_str());
      continue;
    }

    this->add_abbreviation(parts[0], parts[1]);
  }
}

void TransitTracker::set_route_styles_from_text(const std::string &text) {
  this->route_styles_.clear();
  for (const auto &line : split(text, '\n')) {
    auto parts = split(line, ';');
    if (parts.size() != 3) {
      ESP_LOGW(TAG, "Invalid route style line: %s", line.c_str());
      continue;
    }
    uint32_t color;
    if (!parse_hex_color(parts[2], color)) {
      ESP_LOGW(TAG, "Invalid route style color '%s' in line: %s", parts[2].c_str(), line.c_str());
      continue;
    }
    this->add_route_style(parts[0], parts[1], Color(color));
  }
}

void TransitTracker::draw_text_centered_(const char *text, Color color) {
  int display_center_x = this->display_->get_width() / 2;
  int display_center_y = this->display_->get_height() / 2;
  this->display_->print(display_center_x, display_center_y, this->font_, color, display::TextAlign::CENTER, text);
}

void TransitTracker::set_realtime_color(const Color &color) {
  this->realtime_color_ = color;
  this->realtime_color_dark_ = Color(
    (color.r * 0.5),
    (color.g * 0.5),
    (color.b * 0.5)
  );
}

const uint8_t realtime_icon[6][6] = {
  {0, 0, 0, 3, 3, 3},
  {0, 0, 3, 0, 0, 0},
  {0, 3, 0, 0, 2, 2},
  {3, 0, 0, 2, 0, 0},
  {3, 0, 2, 0, 0, 1},
  {3, 0, 2, 0, 1, 1}
};

void HOT TransitTracker::draw_realtime_icon_(int bottom_right_x, int bottom_right_y, unsigned long uptime) {
  const int num_frames = 6;
  const int idle_frame_duration = 3000;
  const int anim_frame_duration = 200;
  const int cycle_duration = idle_frame_duration + (num_frames - 1) * anim_frame_duration;

  unsigned long cycle_time = uptime % cycle_duration;

  int frame;
  if (cycle_time < idle_frame_duration) {
    frame = 0;
  } else {
    frame = 1 + (cycle_time - idle_frame_duration) / anim_frame_duration;
  }

  auto is_segment_lit = [frame](uint8_t segment) {
    switch (segment) {
      case 1: return frame >= 1 && frame <= 3;
      case 2: return frame >= 2 && frame <= 4;
      case 3: return frame >= 3 && frame <= 5;
      default: return false;
    }
  };

  for (uint8_t i = 0; i < 6; ++i) {
    for (uint8_t j = 0; j < 6; ++j) {
      uint8_t segment_number = realtime_icon[i][j];
      if (segment_number == 0) {
        continue;
      }

      Color icon_color = is_segment_lit(segment_number) ? this->realtime_color_ : this->realtime_color_dark_;
      this->display_->draw_pixel_at(bottom_right_x - (5 - j), bottom_right_y - (5 - i), icon_color);
    }
  }
}

void TransitTracker::draw_trip(
    const Trip &trip, int y_offset, int font_height, unsigned long uptime, uint rtc_now,
    bool no_draw, int *headsign_overflow_out, int scroll_cycle_duration
) {
  if (!no_draw) {
    this->display_->print(0, y_offset, this->font_, trip.route_color, display::TextAlign::TOP_LEFT, trip.route_name.c_str());
  }

  int route_width, _;
  this->font_->measure(trip.route_name.c_str(), &route_width, &_, &_, &_);

  auto time_display = this->localization_.fmt_duration_from_now(
    this->display_departure_times_ ? trip.departure_time : trip.arrival_time,
    rtc_now
  );

  int time_width;
  this->font_->measure(time_display.c_str(), &time_width, &_, &_, &_);

  int headsign_clipping_start = route_width + 3;
  int headsign_clipping_end = this->display_->get_width() - time_width - 2;

  if (!no_draw) {
    Color time_color = trip.is_realtime ? this->realtime_color_ : Color(0xa7a7a7);
    this->display_->print(this->display_->get_width() + 1, y_offset, this->font_, time_color, display::TextAlign::TOP_RIGHT, time_display.c_str());
  }

  if (trip.is_realtime) {
    headsign_clipping_end -= 8;

    if(!no_draw) {
      int icon_bottom_right_x = this->display_->get_width() - time_width - 2;
      int icon_bottom_right_y = y_offset + font_height - 6;

      this->draw_realtime_icon_(icon_bottom_right_x, icon_bottom_right_y, uptime);
    }
  }

  int headsign_max_width = headsign_clipping_end - headsign_clipping_start;

  int headsign_actual_width;
  this->font_->measure(trip.headsign.c_str(), &headsign_actual_width, &_, &_, &_);

  int headsign_overflow = headsign_actual_width - headsign_max_width;
  if (headsign_overflow_out) {
    *headsign_overflow_out = headsign_overflow;
  }

  if (no_draw) {
    return;
  }

  int scroll_offset = 0;
  if (headsign_overflow > 0 && scroll_cycle_duration > 0) {
    /// Note: The scroll may jump if headsign_clipping_end changes (e.g. due to the width of the arrival time changing).
    /// This is probably not a big deal, since the display makes sudden changes anyway (e.g. when routes are updated)
    /// and this happens relatively infrequently.

    int scroll_time = headsign_overflow * 1000 / scroll_speed;
    int scroll_cycle_time = uptime % scroll_cycle_duration;

    // Scroll idle (left side - default)
    if(scroll_cycle_time < idle_time_left) {
      // scroll_offset = 0; do nothing
    } else if (scroll_cycle_time < idle_time_left + scroll_time) {
      // Scrolling left
      int time_since_scroll_start = scroll_cycle_time - idle_time_left;
      scroll_offset = time_since_scroll_start * scroll_speed / 1000;
    } else if (scroll_cycle_time < idle_time_left + scroll_time + idle_time_right) {
      // Scroll idle (right side)
      scroll_offset = headsign_overflow;
    } else if (scroll_cycle_time < idle_time_left + 2 * scroll_time + idle_time_right){
      // Scrolling right
      int time_since_scroll_start = scroll_cycle_time - (idle_time_left + scroll_time + idle_time_right);
      scroll_offset = headsign_overflow - (time_since_scroll_start * scroll_speed / 1000);
    } else {
      // Waiting for other headsigns to finish scrolling
      // scroll_offset = 0; do nothing
    }
  }

  this->display_->start_clipping(headsign_clipping_start, 0, headsign_clipping_end, this->display_->get_height());
  this->display_->print(headsign_clipping_start - scroll_offset, y_offset, this->font_, trip.headsign.c_str());
  this->display_->end_clipping();
}

void HOT TransitTracker::draw_schedule() {
  if (this->display_ == nullptr) [[unlikely]] {
    ESP_LOGW(TAG, "No display attached, cannot draw schedule");
    return;
  }

  if (!esphome::network::is_connected()) {
    this->draw_text_centered_("Waiting for network", Color(0x252627));
    return;
  }

  if (!this->rtc_->now().is_valid()) {
    this->draw_text_centered_("Waiting for time sync", Color(0x252627));
    return;
  }

  if (!this->has_valid_pairs_) {
    this->draw_text_centered_("Select a stop", Color(0x252627));
    return;
  }

  if (this->status_has_error()) {
    this->draw_text_centered_("Error loading schedule", Color(0xFE4C5C));
    return;
  }

  if (!this->has_loaded_) {
    this->draw_text_centered_("Loading...", Color(0x252627));
    return;
  }

  std::lock_guard<std::mutex> lock(this->schedule_state_.mutex);

  if (this->schedule_state_.trips.empty()) {
    auto message = this->display_departure_times_ ? "No upcoming departures" : "No upcoming arrivals";
    this->draw_text_centered_(message, Color(0x252627));
    return;
  }

  int nominal_font_height = this->font_->get_ascender() + this->font_->get_descender();
  unsigned long uptime = millis();
  uint rtc_now = this->rtc_->now().timestamp;

  int scroll_cycle_duration = 0;
  if (this->scroll_headsigns_) {
    int largest_headsign_overflow = 0;
    for (const Trip &trip : this->schedule_state_.trips) {
      int headsign_overflow;
      this->draw_trip(trip, 0, nominal_font_height, uptime, rtc_now, true, &headsign_overflow);
      largest_headsign_overflow = std::max(largest_headsign_overflow, headsign_overflow);
    }

    if (largest_headsign_overflow > 0) {
      int longest_scroll_time = largest_headsign_overflow * 1000 / scroll_speed;
      scroll_cycle_duration = idle_time_left + idle_time_right + 2*longest_scroll_time;
    }
  }

  int max_trips_height = (this->limit_ * this->font_->get_ascender()) + ((this->limit_ - 1) * this->font_->get_descender());
  int y_offset = (this->display_->get_height() % max_trips_height) / 2;

  bool has_header_text = !this->header_text_.empty();
  if (has_header_text) {
    this->display_->print(0, y_offset, this->font_, Color(0x00bdbd), display::TextAlign::LEFT, this->header_text_.c_str());
    y_offset += nominal_font_height;
  }

  for (const Trip &trip : this->schedule_state_.trips) {
    this->draw_trip(trip, y_offset, nominal_font_height, uptime, rtc_now, false, nullptr, scroll_cycle_duration);
    y_offset += nominal_font_height;
  }
}

}  // namespace transit_tracker
}  // namespace esphome
