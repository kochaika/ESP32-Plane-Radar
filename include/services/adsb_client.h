#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

/** Longest route tag is "GOT-AMS?" (8) — see services::routes. */
constexpr size_t kRouteBufLen = 12;

/** `callsign` holds the ICAO hex address, not a flight ident. */
constexpr uint8_t kFlagCallsignIsHex = 0x01;

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
  /** Origin-destination codes, filled in by services::routes. */
  char route[kRouteBufLen];
  uint8_t src_flags;
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Mutable view for the route annotator. Not for general use. */
Aircraft* aircraftListMutable();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
