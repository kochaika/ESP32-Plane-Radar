#include "services/route_lookup.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "ui/radar_range.h"

namespace services::routes {

namespace {

// tar1090's default route provider. Plain HTTP on purpose: tar1090 asks clients
// to prefer it for this host, and it spares us an mbedTLS context next to the
// 115 KB frame sprite.
constexpr char kRouteUrl[] = "http://adsb.im/api/0/routeset";
constexpr char kUserAgent[] = "ESP32-Plane-Radar";

constexpr size_t kBatchMax = 12;
constexpr size_t kCacheSize = 96;  // > kMaxAircraft, so ring churn still hits

constexpr int kConnectTimeoutMs = 1200;
constexpr uint16_t kSocketTimeoutMs = 2000;
constexpr unsigned long kRequestTimeoutMs = 4000;
constexpr unsigned long kStreamIdleMs = 1500;
constexpr unsigned long kStreamTotalMs = 4000;

// Longer than kAdsbFetchIntervalMs (3000), matching tar1090's self-throttle.
constexpr uint32_t kMinIntervalMs = 3500;

constexpr uint32_t kTtlResolvedMs = 6UL * 60UL * 60UL * 1000UL;  // 6 h
constexpr uint32_t kTtlUnknownMs = 30UL * 60UL * 1000UL;         // 30 min
constexpr uint32_t kTtlDeferredMs = 60UL * 1000UL;               // 1 min

enum class State : uint8_t { Free, Resolved, Unknown, Deferred };

struct Entry {
  char callsign[9];
  char route[adsb::kRouteBufLen];
  uint32_t resolved_ms;
  uint32_t last_seen_ms;
  State state;
};

Entry s_cache[kCacheSize];
uint32_t s_last_post_ms = 0;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

uint32_t ttlFor(State state) {
  switch (state) {
    case State::Resolved:
      return kTtlResolvedMs;
    case State::Unknown:
      return kTtlUnknownMs;
    case State::Deferred:
      return kTtlDeferredMs;
    default:
      return 0;
  }
}

/**
 * tar1090's normalized_callsign: strip leading zeros from the numeric group of
 * an ALPHA-DIGIT-ALPHA callsign ("SAS0123" -> "SAS123"), so the same flight
 * doesn't occupy two cache slots. Anything else passes through unchanged.
 */
void normalizeCallsign(const char* in, char* out, size_t out_len) {
  const size_t n = strnlen(in, 8);
  size_t alpha = 0;
  while (alpha < n && in[alpha] >= 'A' && in[alpha] <= 'Z') {
    ++alpha;
  }
  size_t digits = alpha;
  while (digits < n && in[digits] >= '0' && in[digits] <= '9') {
    ++digits;
  }
  size_t tail = digits;
  while (tail < n && in[tail] >= 'A' && in[tail] <= 'Z') {
    ++tail;
  }
  if (tail != n || digits == alpha) {
    snprintf(out, out_len, "%.*s", static_cast<int>(n), in);
    return;
  }
  size_t first = alpha;
  while (first + 1 < digits && in[first] == '0') {  // keep at least one digit
    ++first;
  }
  snprintf(out, out_len, "%.*s%.*s", static_cast<int>(alpha), in,
           static_cast<int>(tail - first), in + first);
}

bool isQueryableCallsign(const char* cs) {
  const size_t n = strlen(cs);
  if (n < 3) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    const char c = cs[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
      return false;
    }
  }
  return true;
}

/**
 * Turn `_airport_codes_iata` into a tag string. Returns false for "unknown" and
 * anything else without a separator, which the caller records as Unknown.
 */
bool buildRouteText(const char* iata, bool implausible, char* out,
                    size_t out_len) {
  if (iata == nullptr) {
    return false;
  }

  // Sanitize: this arrives over plain HTTP and ends up in a VLW glyph lookup.
  char clean[32];
  size_t n = 0;
  for (const char* p = iata; *p != '\0' && n + 1 < sizeof(clean); ++p) {
    const char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
      clean[n++] = c;
    }
  }
  clean[n] = '\0';

  const char* first_dash = strchr(clean, '-');
  if (first_dash == nullptr || first_dash == clean || first_dash[1] == '\0') {
    return false;
  }

  const char* last_dash = strrchr(clean, '-');
  char collapsed[32];
  if (first_dash != last_dash) {
    // More than two hops: keep origin and destination only, so the tag stays no
    // wider than the lines already drawn.
    snprintf(collapsed, sizeof(collapsed), "%.*s-%s",
             static_cast<int>(first_dash - clean), clean, last_dash + 1);
  } else {
    snprintf(collapsed, sizeof(collapsed), "%s", clean);
  }

  snprintf(out, out_len, "%s%s", collapsed, implausible ? "?" : "");

  // An over-long code would truncate mid-separator; don't draw a ragged dash.
  size_t len = strlen(out);
  while (len > 0 && out[len - 1] == '-') {
    out[--len] = '\0';
  }
  return len > 0;
}

Entry* findEntry(const char* norm) {
  const uint32_t now = millis();
  for (Entry& e : s_cache) {
    if (e.state == State::Free || strcmp(e.callsign, norm) != 0) {
      continue;
    }
    if (static_cast<uint32_t>(now - e.resolved_ms) >= ttlFor(e.state)) {
      e.state = State::Free;
      e.callsign[0] = '\0';
      return nullptr;
    }
    e.last_seen_ms = now;
    return &e;
  }
  return nullptr;
}

void cacheStore(const char* norm, const char* route, State state) {
  const uint32_t now = millis();
  Entry* slot = nullptr;

  for (Entry& e : s_cache) {
    if (e.state != State::Free && strcmp(e.callsign, norm) == 0) {
      slot = &e;
      break;
    }
  }
  if (slot == nullptr) {
    for (Entry& e : s_cache) {
      if (e.state == State::Free) {
        slot = &e;
        break;
      }
    }
  }
  if (slot == nullptr) {
    for (Entry& e : s_cache) {
      if (static_cast<uint32_t>(now - e.resolved_ms) >= ttlFor(e.state)) {
        slot = &e;
        break;
      }
    }
  }
  if (slot == nullptr) {  // full and all live: evict least recently seen
    slot = &s_cache[0];
    for (Entry& e : s_cache) {
      if (static_cast<uint32_t>(now - e.last_seen_ms) >
          static_cast<uint32_t>(now - slot->last_seen_ms)) {
        slot = &e;
      }
    }
  }

  snprintf(slot->callsign, sizeof(slot->callsign), "%s", norm);
  snprintf(slot->route, sizeof(slot->route), "%s",
           route != nullptr ? route : "");
  slot->resolved_ms = now;
  slot->last_seen_ms = now;
  slot->state = state;
}

bool annotate(adsb::Aircraft* list, size_t count) {
  bool changed = false;
  for (size_t i = 0; i < count; ++i) {
    adsb::Aircraft& ac = list[i];

    char next[adsb::kRouteBufLen];
    next[0] = '\0';

    if (ac.callsign[0] == '\0' ||
        (ac.src_flags & adsb::kFlagCallsignIsHex) != 0) {
      snprintf(next, sizeof(next), "%s", kRouteUnknownLabel);
    } else {
      char norm[9];
      normalizeCallsign(ac.callsign, norm, sizeof(norm));
      const Entry* e = findEntry(norm);
      if (e != nullptr && e->state == State::Resolved) {
        snprintf(next, sizeof(next), "%s", e->route);
      } else if (e != nullptr && e->state == State::Unknown) {
        snprintf(next, sizeof(next), "%s", kRouteUnknownLabel);
      }
      // Deferred or absent: leave blank, the lookup will be retried.
    }

    if (strcmp(ac.route, next) != 0) {
      snprintf(ac.route, sizeof(ac.route), "%s", next);
      changed = true;
    }
  }
  return changed;
}

/**
 * Stream adapter for ArduinoJson that pumps pollNetwork() while it waits.
 *
 * Deliberately NOT derived from Stream: ArduinoJson's Stream specialization
 * blocks inside readBytes() for the client timeout and would freeze the captive
 * portal for the length of the parse. A non-Stream source falls through to the
 * generic Reader, which just forwards read()/readBytes().
 */
class PollingStreamReader {
 public:
  explicit PollingStreamReader(WiFiClient& stream)
      : stream_(&stream), hard_deadline_(millis() + kStreamTotalMs) {}

  int read() {
    char c = 0;
    return readBytes(&c, 1) == 1 ? static_cast<unsigned char>(c) : -1;
  }

  size_t readBytes(char* buf, size_t len) {
    size_t got = 0;
    unsigned long idle_deadline = millis() + kStreamIdleMs;
    while (got < len && millis() < hard_deadline_ && millis() < idle_deadline) {
      pollNetwork();
      const int avail = stream_->available();
      if (avail <= 0) {
        if (!stream_->connected()) {
          break;
        }
        delay(1);
        continue;
      }
      int want = static_cast<int>(len - got);
      if (avail < want) {
        want = avail;
      }
      // avail > 0, so this cannot block.
      const int n = stream_->read(reinterpret_cast<uint8_t*>(buf) + got, want);
      if (n > 0) {
        got += static_cast<size_t>(n);
        idle_deadline = millis() + kStreamIdleMs;
      }
    }
    return got;
  }

 private:
  WiFiClient* stream_;
  unsigned long hard_deadline_;
};

int performPostWithPoll(HTTPClient& http, const String& body) {
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  int code = 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    pollNetwork();
    code = http.POST(body);
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    if (millis() >= deadline) {
      break;
    }
    delay(5);
  }
  return code;
}

int indexOfSent(char sent[][9], size_t count, const char* callsign) {
  if (callsign == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < count; ++i) {
    if (sent[i][0] != '\0' && strcmp(sent[i], callsign) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

/** Anything still unmatched never came back — retry it in kTtlDeferredMs. */
void deferUnmatched(char sent[][9], size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (sent[i][0] != '\0') {
      cacheStore(sent[i], "", State::Deferred);
    }
  }
}

void postBatch(char sent[][9], const float* lats, const float* lons,
               size_t count) {
  String body;
  body.reserve(20 + count * 48);
  body = "{\"planes\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i != 0) {
      body += ',';
    }
    body += "{\"callsign\":\"";
    body += sent[i];
    body += "\",\"lat\":";
    body += String(lats[i], 3);
    body += ",\"lng\":";
    body += String(lons[i], 3);
    body += '}';
  }
  body += "]}";

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, kRouteUrl)) {
    Serial.println("routes: http.begin failed");
    deferUnmatched(sent, count);
    return;
  }

  http.useHTTP10(true);  // non-chunked, so the raw stream is the JSON
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kSocketTimeoutMs);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", kUserAgent);

  const int code = performPostWithPoll(http, body);
  if (code != HTTP_CODE_OK) {
    Serial.printf("routes: HTTP %d\n", code);
    http.end();
    deferUnmatched(sent, count);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    deferUnmatched(sent, count);
    return;
  }

  // Array-rooted filter: element 0 is applied to every element, and everything
  // outside it (notably the nine-field _airports objects) is skipped, not
  // allocated. That is what keeps a ~8 KB response inside ~2 KB of document.
  JsonDocument filter;
  JsonObject wanted = filter.add<JsonObject>();
  wanted["callsign"] = true;
  wanted["_airport_codes_iata"] = true;
  wanted["plausible"] = true;

  PollingStreamReader reader(*stream);
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  http.end();

  if (err && err != DeserializationError::IncompleteInput) {
    Serial.printf("routes: JSON parse error: %s\n", err.c_str());
    deferUnmatched(sent, count);
    return;
  }

  size_t resolved = 0;
  for (JsonObject entry : doc.as<JsonArray>()) {
    // Match on the echoed callsign, not array position: a route attached to the
    // wrong aircraft would look completely plausible on screen.
    const int idx = indexOfSent(sent, count, entry["callsign"].as<const char*>());
    if (idx < 0) {
      continue;
    }

    const bool implausible =
        entry["plausible"].is<bool>() && !entry["plausible"].as<bool>();
    char text[adsb::kRouteBufLen];
    if (buildRouteText(entry["_airport_codes_iata"].as<const char*>(),
                       implausible, text, sizeof(text))) {
      cacheStore(sent[idx], text, State::Resolved);
      ++resolved;
    } else {
      cacheStore(sent[idx], kRouteUnknownLabel, State::Unknown);
    }
    sent[idx][0] = '\0';
  }

  deferUnmatched(sent, count);
  Serial.printf("routes: %u queried, %u resolved\n",
                static_cast<unsigned>(count), static_cast<unsigned>(resolved));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

void annotateFromCache(adsb::Aircraft* list, size_t count) {
  annotate(list, count);
}

bool resolvePending(adsb::Aircraft* list, size_t count) {
  if (!ui::radar::showRoutes() || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  char sent[kBatchMax][9];
  float lats[kBatchMax];
  float lons[kBatchMax];
  size_t batch_n = 0;

  for (size_t i = 0; i < count && batch_n < kBatchMax; ++i) {
    const adsb::Aircraft& ac = list[i];
    if (ac.callsign[0] == '\0' ||
        (ac.src_flags & adsb::kFlagCallsignIsHex) != 0) {
      continue;  // no flight ident: resolved locally, never queried
    }

    char norm[9];
    normalizeCallsign(ac.callsign, norm, sizeof(norm));
    if (!isQueryableCallsign(norm) || findEntry(norm) != nullptr) {
      continue;
    }

    bool duplicate = false;
    for (size_t j = 0; j < batch_n; ++j) {
      if (strcmp(sent[j], norm) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    snprintf(sent[batch_n], sizeof(sent[batch_n]), "%s", norm);
    lats[batch_n] = ac.lat;
    lons[batch_n] = ac.lon;
    ++batch_n;
  }

  if (batch_n == 0) {
    return false;  // steady state: the cache covers everything, no request
  }

  const uint32_t now = millis();
  if (s_last_post_ms != 0 &&
      static_cast<uint32_t>(now - s_last_post_ms) < kMinIntervalMs) {
    return false;
  }
  s_last_post_ms = now;

  postBatch(sent, lats, lons, batch_n);
  return annotate(list, count);
}

void clearCache() {
  for (Entry& e : s_cache) {
    e.state = State::Free;
    e.callsign[0] = '\0';
    e.route[0] = '\0';
  }
  s_last_post_ms = 0;
}

}  // namespace services::routes
