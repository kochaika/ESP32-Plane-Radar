#include "ui/radar_projection.h"

#include <cmath>

#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::radar {

namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

// Rotation of the map frame, evaluated once at startup.
const float kSinUp = sinf(kUpBearingDeg * kDegToRad);
const float kCosUp = cosf(kUpBearingDeg * kDegToRad);

}  // namespace

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  // Longitude degrees shrink toward the poles; scale by cos(latitude) so
  // east-west distance isn't overstated away from the equator.
  const float center_lat_rad =
      static_cast<float>(services::location::lat()) * kDegToRad;
  *dx_km = static_cast<float>(lon - services::location::lon()) * kKmPerDeg *
           cosf(center_lat_rad);
  *dy_km = static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

void rotateKmToScreenFrame(float dx_km, float dy_km, float* out_x_km,
                           float* out_y_km) {
  *out_x_km = dx_km * kCosUp - dy_km * kSinUp;
  *out_y_km = dx_km * kSinUp + dy_km * kCosUp;
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  float x_km = 0.0f;
  float y_km = 0.0f;
  rotateKmToScreenFrame(dx_km, dy_km, &x_km, &y_km);

  *out_x = kCenterX + static_cast<int>(lroundf(x_km * px_per_km));
  *out_y = kCenterY - static_cast<int>(lroundf(y_km * px_per_km));
}

float screenBearingDeg(float true_bearing_deg) {
  return true_bearing_deg - kUpBearingDeg;
}

}  // namespace ui::radar
