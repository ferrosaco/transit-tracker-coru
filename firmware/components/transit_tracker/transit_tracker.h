#pragma once

#include <atomic>
#include <ctime>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/component.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/time/real_time_clock.h"

#include "schedule_state.h"
#include "localization.h"
#include "itranvias.h"

namespace esphome {
namespace transit_tracker {

struct RouteStyle {
  std::string name;
  Color color;
};

class TransitTracker : public Component {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    void on_shutdown() override;

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    bool is_connected() const { return this->last_poll_ok_; }
    void reconnect(const char *reason);
    void close(bool fully = false);

    void draw_schedule();

    Localization* get_localization() { return &this->localization_; }

    void set_display(display::Display *display) { display_ = display; }
    void set_font(font::Font *font) { font_ = font; }
    void set_rtc(time::RealTimeClock *rtc) { rtc_ = rtc; }

    void set_base_url(const std::string &base_url) { base_url_ = base_url; }
    void set_feed_code(const std::string &feed_code) { feed_code_ = feed_code; }
    void set_display_departure_times(bool display_departure_times) { display_departure_times_ = display_departure_times; }
    void set_schedule_string(const std::string &schedule_string);
    void set_list_mode(const std::string &list_mode);
    void set_limit(int limit) { limit_ = limit; }
    void set_scroll_headsigns(bool scroll_headsigns) { scroll_headsigns_ = scroll_headsigns; }

    void set_header_text(const std::string &header_text) { header_text_ = header_text; }
    void set_unit_display(UnitDisplay unit_display) { this->localization_.set_unit_display(unit_display); }
    void add_abbreviation(const std::string &from, const std::string &to) { abbreviations_[from] = to; }
    void add_header(const std::string &name, const std::string &value) { extra_headers_.emplace_back(name, value); }
    void set_default_route_color(const Color &color) { default_route_color_ = color; }
    void add_route_style(const std::string &route_id, const std::string &name, const Color &color) { route_styles_[route_id] = RouteStyle{name, color}; }

    void set_abbreviations_from_text(const std::string &text);
    void set_route_styles_from_text(const std::string &text);

    void set_realtime_color(const Color &color);

  protected:
    static constexpr int scroll_speed = 10; // pixels/second
    static constexpr int idle_time_left = 5000;
    static constexpr int idle_time_right = 1000;

    void draw_text_centered_(const char *text, Color color);
    void draw_realtime_icon_(int bottom_right_x, int bottom_right_y, unsigned long now);

    void draw_trip(
      const Trip &trip, int y_offset, int font_height, unsigned long uptime, uint rtc_now,
      bool no_draw = false, int *headsign_overflow_out = nullptr, int scroll_cycle_duration = 0
    );

    Localization localization_{};
    ScheduleState schedule_state_;

    display::Display *display_;
    font::Font *font_;
    time::RealTimeClock *rtc_;

    // HTTPS to iTranvías runs on its own task. The ESP32 task watchdog panics
    // after 5s, and the TLS handshake plus the catalog download take longer
    // than that on the main loop. A panic reboot is what made the screen
    // alternate between "Waiting for network" and "Loading...".
    enum class FetchFailure : uint8_t { NONE, CATALOG, ARRIVALS };

    struct PollJob {
      uint32_t generation{0};
      time_t now{0};
      int limit{3};
      std::string schedule;
      std::string list_mode;
      std::map<std::string, std::string> abbreviations;
      std::map<std::string, RouteStyle> route_styles;
      Color default_route_color{};
    };

    struct FetchResult {
      uint32_t generation{0};
      bool valid_pairs{false};
      FetchFailure failure{FetchFailure::NONE};
      std::vector<Trip> trips;
    };

    static void poll_task_thunk_(void *param);
    void poll_task_();
    PollJob make_job_(time_t now) const;
    FetchResult fetch_schedule_(const PollJob &job);
    std::vector<Trip> trips_from_(const std::vector<itranvias::BuiltTrip> &trips, const PollJob &job) const;
    void apply_result_();
    void apply_result_now_(FetchResult result);
    bool ingest_network_(const char *body, size_t length);
    bool ingest_buses_(const char *body, size_t length, std::vector<itranvias::LiveBus> &out);
    void request_refresh_();

    std::atomic<bool> refresh_requested_{false};
    std::atomic<bool> fully_closed_{false};
    std::atomic<bool> poll_busy_{false};
    std::atomic<uint32_t> generation_{1};
    TaskHandle_t poll_task_handle_{nullptr};
    StackType_t *poll_stack_{nullptr};
    StaticTask_t poll_tcb_{};
    PollJob job_{};
    std::mutex result_mutex_;
    bool result_ready_{false};
    FetchResult result_{};
    bool network_ready_ = false;
    bool has_valid_pairs_ = false;
    bool has_loaded_ = false;
    bool last_poll_ok_ = false;
    unsigned long network_loaded_at_ = 0;

    itranvias::Network network_{};

    std::string base_url_;
    std::vector<std::pair<std::string, std::string>> extra_headers_;
    std::string feed_code_;
    std::string schedule_string_;
    std::string list_mode_;
    bool display_departure_times_ = true;
    int limit_ = 3;

    std::string header_text_;
    std::map<std::string, std::string> abbreviations_;
    Color default_route_color_ = Color(0x028e51);
    std::map<std::string, RouteStyle> route_styles_;
    bool scroll_headsigns_ = false;

    Color realtime_color_ = Color(0x20FF00);
    Color realtime_color_dark_ = Color(0x00A700);
};


}  // namespace transit_tracker
}  // namespace esphome