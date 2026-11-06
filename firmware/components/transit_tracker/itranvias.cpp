#include "itranvias.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace itranvias {

static std::string trim(const std::string &value) {
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    begin++;
  }
  size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    end--;
  }
  return value.substr(begin, end - begin);
}

static bool parse_int(const std::string &value, int &out) {
  if (value.empty()) {
    return false;
  }
  char *end = nullptr;
  long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    return false;
  }
  out = static_cast<int>(parsed);
  return true;
}

bool parse_local_id(const std::string &value, int &out) {
  std::string text = trim(value);
  auto colon = text.find(':');
  if (colon != std::string::npos) {
    if (text.substr(0, colon) != "coruna") {
      return false;
    }
    text = text.substr(colon + 1);
  }
  return parse_int(text, out);
}

bool parse_route_stop_pairs(const std::string &raw, std::vector<RouteStopPair> &out) {
  out.clear();
  size_t cursor = 0;
  while (cursor <= raw.size()) {
    size_t semi = raw.find(';', cursor);
    std::string chunk = trim(raw.substr(cursor, semi == std::string::npos ? std::string::npos : semi - cursor));
    cursor = semi == std::string::npos ? raw.size() + 1 : semi + 1;
    if (chunk.empty()) {
      continue;
    }

    std::string fields[3];
    int count = 0;
    size_t field_cursor = 0;
    bool too_many = false;
    while (field_cursor <= chunk.size()) {
      size_t comma = chunk.find(',', field_cursor);
      if (count >= 3) {
        too_many = true;
        break;
      }
      fields[count++] = trim(chunk.substr(field_cursor, comma == std::string::npos ? std::string::npos : comma - field_cursor));
      if (comma == std::string::npos) {
        break;
      }
      field_cursor = comma + 1;
    }
    if (too_many || count < 2) {
      continue;
    }

    RouteStopPair pair;
    int offset = 0;
    if (!parse_local_id(fields[0], pair.line_id) || !parse_local_id(fields[1], pair.stop_local)) {
      continue;
    }
    if (count == 3 && !parse_int(fields[2], offset)) {
      continue;
    }
    pair.route_id = fields[0];
    pair.stop_id = fields[1];
    pair.offset = offset;
    out.push_back(pair);
  }
  return true;
}

bool parse_minutes(const std::string &value, int &minutes, bool &imminent) {
  std::string text = trim(value);
  if (text.empty()) {
    return false;
  }
  if (text[0] == '<') {
    minutes = 0;
    imminent = true;
    return true;
  }
  imminent = false;
  return parse_int(text, minutes);
}

bool eta_timestamp(std::time_t now, const LiveBus &bus, int offset, std::time_t &out) {
  bool imminent = bus.imminent || bus.minutes <= 0;
  std::time_t raw = now + (imminent ? 20 : static_cast<std::time_t>(bus.minutes) * 60);
  std::time_t adjusted = raw + offset;
  if (adjusted <= now) {
    return false;
  }
  out = imminent ? adjusted : adjusted + 59;
  return true;
}

int direction_for_bus(const Line &line, int stop_id, int last_stop_id) {
  int best_distance = -1;
  int best_index = -1;
  std::vector<int> present;
  for (size_t index = 0; index < line.routes.size(); index++) {
    const auto &route = line.routes[index];
    auto stop_at = std::find(route.begin(), route.end(), stop_id);
    if (stop_at == route.end()) {
      continue;
    }
    present.push_back(static_cast<int>(index));
    if (last_stop_id < 0) {
      continue;
    }
    auto last_at = std::find(route.begin(), route.end(), last_stop_id);
    if (last_at == route.end()) {
      continue;
    }
    int stop_index = static_cast<int>(stop_at - route.begin());
    int last_index = static_cast<int>(last_at - route.begin());
    if (last_index <= stop_index) {
      int distance = stop_index - last_index;
      if (best_distance < 0 || distance < best_distance) {
        best_distance = distance;
        best_index = static_cast<int>(index);
      }
    }
  }
  if (best_index >= 0) {
    return best_index;
  }
  if (present.empty()) {
    return -1;
  }
  return present[0];
}

std::string headsign_for_route(const Line &line, int route_index, const std::map<int, std::string> &stops) {
  if (route_index == 0 && !line.destination.empty()) {
    return line.destination;
  }
  if (route_index == 1 && !line.origin.empty()) {
    return line.origin;
  }
  if (route_index >= 0 && static_cast<size_t>(route_index) < line.routes.size()) {
    const auto &route = line.routes[route_index];
    if (!route.empty()) {
      auto last = stops.find(route.back());
      if (last != stops.end()) {
        return last->second;
      }
    }
  }
  if (!line.destination.empty()) {
    return line.destination;
  }
  if (!line.origin.empty()) {
    return line.origin;
  }
  return line.name;
}

std::vector<BuiltTrip> build_trips(const Network &network, const std::vector<RouteStopPair> &pairs,
                                   const std::map<int, std::vector<LiveBus>> &buses_by_stop, std::time_t now,
                                   int limit, const std::string &list_mode) {
  std::vector<BuiltTrip> trips;
  for (const auto &pair : pairs) {
    auto line_at = network.lines.find(pair.line_id);
    if (line_at == network.lines.end()) {
      continue;
    }
    const Line &line = line_at->second;
    auto buses_at = buses_by_stop.find(pair.stop_local);
    if (buses_at == buses_by_stop.end()) {
      continue;
    }
    for (const auto &bus : buses_at->second) {
      if (bus.line_id != pair.line_id) {
        continue;
      }
      std::time_t when = 0;
      if (!eta_timestamp(now, bus, pair.offset, when)) {
        continue;
      }
      int direction = direction_for_bus(line, pair.stop_local, bus.last_stop_id);
      BuiltTrip trip;
      trip.route_id = pair.route_id;
      trip.route_name = line.name;
      trip.route_color = line.color;
      trip.headsign = direction >= 0 ? headsign_for_route(line, direction, network.stop_names)
                                      : (!line.destination.empty() ? line.destination
                                                                   : (!line.origin.empty() ? line.origin : line.name));
      trip.direction_id = direction >= 0 ? std::to_string(direction) : "";
      trip.when = when;
      trips.push_back(trip);
    }
  }

  std::stable_sort(trips.begin(), trips.end(), [](const BuiltTrip &left, const BuiltTrip &right) {
    return left.when < right.when;
  });

  if (list_mode == "nextPerRoute") {
    std::vector<BuiltTrip> filtered;
    std::vector<std::string> seen;
    for (const auto &trip : trips) {
      std::string key = trip.route_id + "-" + trip.direction_id;
      if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
        continue;
      }
      seen.push_back(key);
      filtered.push_back(trip);
    }
    trips.swap(filtered);
  }

  if (limit >= 0 && static_cast<size_t>(limit) < trips.size()) {
    trips.resize(static_cast<size_t>(limit));
  }
  return trips;
}

}  // namespace itranvias
