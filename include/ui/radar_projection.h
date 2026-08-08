#pragma once

namespace ui::radar {

/**
 * True bearing (deg) drawn at the top of the screen.
 *
 *     0 = north up   90 = east up   180 = south up   270 = west up
 *
 * Only the plot rotates — text stays upright, so this is not the same as
 * tft.setRotation(), which would turn the labels upside down too.
 * Must be a quarter turn; drawCardinalLabels() derives N/E/S/W from it.
 */
constexpr float kUpBearingDeg = 180.0f;

/** East/north offset of lat/lon from the radar center, in km. */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

/**
 * Rotate an east/north km offset into the screen frame, so that a target on
 * bearing kUpBearingDeg ends up straight above the center. Output is still
 * east/north-like: +x is screen right, +y is screen up.
 */
void rotateKmToScreenFrame(float dx_km, float dy_km, float* out_x_km,
                           float* out_y_km);

/** Flat lat/lon as pixel x/y: 1° ≈ 111 km, kUpBearingDeg = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

/** True bearing/heading (deg) as an on-screen angle, 0 = straight up. */
float screenBearingDeg(float true_bearing_deg);

}  // namespace ui::radar
