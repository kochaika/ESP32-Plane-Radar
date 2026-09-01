#pragma once

#include <cstddef>

#include "services/adsb_client.h"

namespace services::routes {

/** Shown when the provider has no route, or the aircraft has no flight ident. */
constexpr char kRouteUnknownLabel[] = "N/A-N/A";

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fill each aircraft's `route` from the cache. Local only, no network. */
void annotateFromCache(adsb::Aircraft* list, size_t count);

/**
 * Look up routes still missing from the cache: at most one batched POST, then
 * re-annotate. Returns true if any route string changed, so the caller can
 * repaint. Never fails hard — on any error the radar just keeps its old tags.
 */
bool resolvePending(adsb::Aircraft* list, size_t count);

void clearCache();

}  // namespace services::routes
