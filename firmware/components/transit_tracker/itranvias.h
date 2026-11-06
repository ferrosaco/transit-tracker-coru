#pragma once

#include <ctime>
#include <map>
#include <string>
#include <vector>

// Pure schedule logic for the iTranvías queryitr API. No ESPHome types, so the
// same file can be compiled on the device and checked with a host test.

namespace itranvias {

struct Line {
  int id = 0;
  std::string name;
  std::string origin;
  std::string destination;
  std::string color;
  std::vector<std::vector<int>> routes;
};

struct Network {
  std::map<int, Line> lines;
  std::map<int, std::string> stop_names;
};

struct LiveBus {
  int line_id = 0;
  std::string vehicle;
  int minutes = 0;
  bool imminent = false;
  int last_stop_id = -1;
};

struct RouteStopPair {
  std::string route_id;
  std::string stop_id;
  int line_id = 0;
  int stop_local = 0;
  int offset = 0;
};

struct BuiltTrip {
  std::string route_id;
  std::string route_name;
  std::string route_color;
  std::string headsign;
  std::string direction_id;
  std::time_t when = 0;
};

// Accepts "coruna:100" or a bare "100". Any other feed prefix is rejected so a
// leftover GTFS selection is not sent to iTranvías as if it were the same id.
bool parse_local_id(const std::string &value, int &out);

bool parse_route_stop_pairs(const std::string &raw, std::vector<RouteStopPair> &out);

bool parse_minutes(const std::string &value, int &minutes, bool &imminent);

// Returns false when the walk offset means the bus can no longer be caught.
bool eta_timestamp(std::time_t now, const LiveBus &bus, int offset, std::time_t &out);

// -1 when the stop is not on any route of the line.
int direction_for_bus(const Line &line, int stop_id, int last_stop_id);

std::string headsign_for_route(const Line &line, int route_index, const std::map<int, std::string> &stops);

std::vector<BuiltTrip> build_trips(const Network &network, const std::vector<RouteStopPair> &pairs,
                                   const std::map<int, std::vector<LiveBus>> &buses_by_stop, std::time_t now,
                                   int limit, const std::string &list_mode);

}  // namespace itranvias
