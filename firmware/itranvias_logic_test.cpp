#include "components/transit_tracker/itranvias.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL " << message << "\n";
    failures++;
  }
}

itranvias::Network fixture() {
  itranvias::Network network;
  itranvias::Line line100;
  line100.id = 100;
  line100.name = "1";
  line100.origin = "Abente y Lago";
  line100.destination = "Castrillón";
  line100.color = "982135";
  line100.routes = {{523, 180, 1, 2, 68}, {68, 69, 523}};
  itranvias::Line line200;
  line200.id = 200;
  line200.name = "2";
  line200.origin = "Abente y Lago";
  line200.destination = "Los Castros";
  line200.color = "0055AA";
  line200.routes = {{1, 8}, {8, 1}};
  network.lines[100] = line100;
  network.lines[200] = line200;
  network.stop_names = {{1, "Puerta Real"}, {8, "Los Castros"}, {68, "Castrillón"}, {69, "Vuelta"},
                         {180, "Antes"},     {523, "Abente y Lago"}};
  return network;
}

std::map<int, std::vector<itranvias::LiveBus>> arrivals() {
  return {{1,
           {{100, "378", 6, false, 180},
            {100, "421", 4, false, 69},
            {100, "1", 0, true, 180},
            {200, "391", 14, false, 1}}}};
}

}  // namespace

int main() {
  int id = 0;
  expect(itranvias::parse_local_id("coruna:100", id) && id == 100, "coruna id");
  expect(itranvias::parse_local_id("1", id) && id == 1, "bare id");
  expect(!itranvias::parse_local_id("coru:139", id), "old gtfs feed is ignored");

  std::vector<itranvias::RouteStopPair> pairs;
  itranvias::parse_route_stop_pairs("coru:10,coru:139;coruna:100,coruna:1,-120", pairs);
  expect(pairs.size() == 1 && pairs[0].line_id == 100 && pairs[0].stop_local == 1 && pairs[0].offset == -120,
         "only the coruna pair is kept");

  auto network = fixture();
  expect(itranvias::direction_for_bus(network.lines[100], 1, 180) == 0, "outbound direction");
  expect(itranvias::direction_for_bus(network.lines[100], 1, 69) == 0, "fallback when last stop is on another route");
  expect(itranvias::direction_for_bus(network.lines[200], 1, 8) == 1, "return direction");

  const std::time_t now = 1700000000;
  std::time_t when = 0;
  itranvias::LiveBus too_close{100, "1", 5, false, 180};
  expect(!itranvias::eta_timestamp(now, too_close, -300, when), "five minutes with a five minute walk is hidden");
  itranvias::LiveBus catchable{100, "1", 6, false, 180};
  expect(itranvias::eta_timestamp(now, catchable, -300, when) && when == now + 60 + 59, "six minutes stays in the same minute bucket");

  auto trips = itranvias::build_trips(network, pairs, arrivals(), now, 10, "sequential");
  expect(trips.size() == 2, "imminent bus with a two minute walk is hidden, two remain");
  expect(!trips.empty() && trips[0].route_name == "1" && trips[0].headsign == "Castrillón", "commercial name and headsign");

  std::vector<itranvias::RouteStopPair> both;
  itranvias::parse_route_stop_pairs("coruna:100,coruna:1;coruna:200,coruna:1", both);
  auto grouped = itranvias::build_trips(network, both, arrivals(), now, 10, "nextPerRoute");
  expect(grouped.size() == 2, "one trip per route and direction");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
