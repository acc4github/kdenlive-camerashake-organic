#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

// =============================================
// HARDCODED SPEED BIAS SETTING
// Range: -100 (only slowdown) to +100 (only speedup)
// Default: 0 (perfectly symmetrical)
// =============================================
#define SPEED_BIAS_PERCENT -99.0

// Ensure M_PI is defined (for cross‑platform safety)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
    #include <frei0r.h>

    struct CameraShakeInstance {
        double params[10];          // 0: amp_x, 1: amp_y, 2: rotation,
                                    // 3: blur, 4: speed, 5: speed_noise,
                                    // 6: zoom, 7: auto_zoom,
                                    // 8: edge_smoothing, 9: seed

        unsigned int width, height;
        double scale_factor;

        // Content bounds (for edge clamping)
        int content_min_x, content_min_y;
        int content_max_x, content_max_y;
        bool bounds_calculated;

        // Cached auto‑zoom value & dirty flag
        double cached_zoom;
        bool zoom_dirty;
    };

    int f0r_init(void) { return 1; }
    void f0r_deinit(void) {}

    void f0r_get_plugin_info(f0r_plugin_info_t* info) {
        info->name = "Camera Shake Organic (Edge Smoothing + Auto Zoom)";
        info->author = "Modified by acc4commissions and Grok 4.3";
        info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
        info->color_model = F0R_COLOR_MODEL_RGBA8888;
        info->frei0r_version = FREI0R_MAJOR_VERSION;
        info->major_version = 2;
        info->minor_version = 6;
        info->num_params = 10;
        info->explanation = "Organic irregular camera shake with noise on movement, rotation, blur, zoom and occasional speed slowdown. ";
    }

    void f0r_get_param_info(f0r_param_info_t* info, int param_index) {
        static const char* names[10] = {
            "amplitude_x", "amplitude_y", "rotation", "blur",
            "speed", "speed_noise", "zoom", "auto_zoom",
            "edge_smoothing", "seed"
        };
        info->name = names[param_index];
        if (param_index == 7 || param_index == 8)
            info->type = F0R_PARAM_BOOL;
        else
            info->type = F0R_PARAM_DOUBLE;
    }

    f0r_instance_t f0r_construct(unsigned int width, unsigned int height) {
        CameraShakeInstance* inst = new CameraShakeInstance();

        inst->params[0] = 8.0;
        inst->params[1] = 8.0;
        inst->params[2] = 2.0;
        inst->params[3] = 0.0;
        inst->params[4] = 55.0;
        inst->params[5] = 0.0;
        inst->params[6] = 100.0;
        inst->params[7] = 0.0;
        inst->params[8] = 0.0;
        inst->params[9] = 0.0;

        inst->width = width;
        inst->height = height;
        inst->scale_factor = width / 1920.0;

        inst->bounds_calculated = false;
        inst->content_min_x = 0;
        inst->content_max_x = (int)width - 1;
        inst->content_min_y = 0;
        inst->content_max_y = (int)height - 1;

        // Cache initial zoom (will be computed on first update)
        inst->cached_zoom = 1.0;
        inst->zoom_dirty = true;  // force computation on first frame

        return (f0r_instance_t)inst;
    }

    void f0r_destruct(f0r_instance_t instance) {
        delete (CameraShakeInstance*)instance;
    }

    void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        inst->params[param_index] = *((double*)param);

        // Invalidate cached zoom if any relevant parameter changes
        if (param_index == 0 || param_index == 1 || param_index == 2 || param_index == 7) {
            inst->zoom_dirty = true;
        }

        // Bounds depend on zoom/edge_smoothing/seed? Not really, but keep as before.
        if (param_index == 6 || param_index == 8 || param_index == 9) {
            inst->bounds_calculated = false;
        }
    }

    void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        *((double*)param) = ((CameraShakeInstance*)instance)->params[param_index];
    }

    // ------------------------------------------------------------
    //  HELPER FUNCTIONS (unchanged)
    // ------------------------------------------------------------
    double organic_noise(double t) {
        double n = sin(t) * 0.5 +
                   sin(t * 2.31) * 0.25 +
                   sin(t * 4.57) * 0.15 +
                   sin(t * 7.13) * 0.08;
        n += sin(t * 0.37) * 0.3;
        return n;
    }

    double integral_organic_noise(double t) {
        double I = -0.5 * cos(t)
                   - (0.25 / 2.31) * cos(2.31 * t)
                   - (0.15 / 4.57) * cos(4.57 * t)
                   - (0.08 / 7.13) * cos(7.13 * t)
                   - (0.3  / 0.37) * cos(0.37 * t);
        return I;
    }

    void calculate_content_bounds(CameraShakeInstance* inst, const uint32_t* buf, int bw, int bh) {
        if (inst->bounds_calculated) return;

        int left = bw, right = -1, top = bh, bottom = -1;
        for (int x = 0; x < bw; ++x) {
            const uint8_t* p = (const uint8_t*)&buf[(bh/2) * bw + x];
            if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        for (int y = 0; y < bh; ++y) {
            const uint8_t* p = (const uint8_t*)&buf[y * bw + (bw/2)];
            if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
        if (left > right || top > bottom || left >= bw || right < 0) {
            left = 0; right = bw - 1; top = 0; bottom = bh - 1;
        }
        inst->content_min_x = left;
        inst->content_max_x = right;
        inst->content_min_y = top;
        inst->content_max_y = bottom;
        inst->bounds_calculated = true;
    }

    inline void content_clamped_smoothed_sample(const uint32_t* buf, int bw, int bh,
                                                int min_x, int min_y, int max_x, int max_y,
                                                double fx, double fy, uint8_t* out) {
        if (fx < min_x - 1.0 || fx > max_x + 1.0 || fy < min_y - 1.0 || fy > max_y + 1.0) {
            out[0] = out[1] = out[2] = out[3] = 0;
            return;
        }
        int x0 = (int)fx;
        int y0 = (int)fy;
        double dx = fx - x0;
        double dy = fy - y0;
        x0 = std::max(min_x, std::min(x0, max_x - 1));
        y0 = std::max(min_y, std::min(y0, max_y - 1));
        int x1 = std::min(x0 + 1, max_x);
        int y1 = std::min(y0 + 1, max_y);
        const uint8_t* p00 = (const uint8_t*)&buf[y0 * bw + x0];
        const uint8_t* p10 = (const uint8_t*)&buf[y0 * bw + x1];
        const uint8_t* p01 = (const uint8_t*)&buf[y1 * bw + x0];
        const uint8_t* p11 = (const uint8_t*)&buf[y1 * bw + x1];
        double w00 = (1.0 - dx) * (1.0 - dy);
        double w10 = dx * (1.0 - dy);
        double w01 = (1.0 - dx) * dy;
        double w11 = dx * dy;
        out[0] = (uint8_t)(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11 + 0.5);
        out[1] = (uint8_t)(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11 + 0.5);
        out[2] = (uint8_t)(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11 + 0.5);
        out[3] = (uint8_t)(p00[3]*w00 + p10[3]*w10 + p01[3]*w01 + p11[3]*w11 + 0.5);
    }

    inline void fast_blur_sample(const uint32_t* buf, int bw, int bh,
                                 int min_x, int min_y, int max_x, int max_y,
                                 int ix, int iy, int blur_radius, uint8_t* out) {
        if (blur_radius <= 0) {
            content_clamped_smoothed_sample(buf, bw, bh, min_x, min_y, max_x, max_y, (double)ix, (double)iy, out);
            return;
        }
        int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        int samples = 0;
        const int step = (blur_radius > 3) ? 2 : 1;
        // Horizontal
        for (int i = -blur_radius; i <= blur_radius; i += step) {
            int sx = ix + i;
            if (sx >= min_x && sx <= max_x) {
                const uint8_t* p = (const uint8_t*)&buf[iy * bw + sx];
                sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                samples++;
            }
        }
        // Vertical
        for (int i = -blur_radius; i <= blur_radius; i += step) {
            if (i == 0) continue;
            int sy = iy + i;
            if (sy >= min_y && sy <= max_y) {
                const uint8_t* p = (const uint8_t*)&buf[sy * bw + ix];
                sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                samples++;
            }
        }
        // Diagonals
        for (int i = -blur_radius; i <= blur_radius; i += step) {
            if (i == 0) continue;
            int sx1 = ix + i, sy1 = iy + i;
            if (sx1 >= min_x && sx1 <= max_x && sy1 >= min_y && sy1 <= max_y) {
                const uint8_t* p = (const uint8_t*)&buf[sy1 * bw + sx1];
                sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                samples++;
            }
            int sx2 = ix + i, sy2 = iy - i;
            if (sx2 >= min_x && sx2 <= max_x && sy2 >= min_y && sy2 <= max_y) {
                const uint8_t* p = (const uint8_t*)&buf[sy2 * bw + sx2];
                sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                samples++;
            }
        }
        if (samples > 1) {
            int half = samples / 2;
            out[0] = (uint8_t)((sum_r + half) / samples);
            out[1] = (uint8_t)((sum_g + half) / samples);
            out[2] = (uint8_t)((sum_b + half) / samples);
            out[3] = (uint8_t)((sum_a + half) / samples);
        } else {
            content_clamped_smoothed_sample(buf, bw, bh, min_x, min_y, max_x, max_y, (double)ix, (double)iy, out);
        }
    }

    int compute_blur_radius(CameraShakeInstance* inst, double t) {
        double blur_noise = organic_noise(t * 0.58);
        double normalized = (blur_noise + 1.20) / 2.40;
        double blur_factor = normalized * normalized;
        const double threshold = 0.25;
        const double compression = 0.25;
        if (blur_factor > threshold)
            blur_factor = threshold + (blur_factor - threshold) * compression;
        double adjusted = fmax(blur_factor - 0.2, 0.0);
        return (int)(inst->params[3] * adjusted * inst->scale_factor * 4);
    }

    // ------------------------------------------------------------
    //  MAIN UPDATE
    // ------------------------------------------------------------
    void f0r_update(f0r_instance_t instance, double time, const uint32_t* inframe, uint32_t* outframe) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        const int w = inst->width;
        const int h = inst->height;
        const double scale = inst->scale_factor;

        // ---- Update cached auto‑zoom if dirty ----
        if (inst->zoom_dirty) {
            if (inst->params[7] > 0.5) { // auto_zoom enabled
                const double amp_x = inst->params[0];
                const double amp_y = inst->params[1];
                const double rot_slider = inst->params[2];

                const double dx_max = 1.28 * amp_x * scale;
                const double dy_max = 1.28 * amp_y * scale;

                double theta_max = 1.28 * rot_slider * (M_PI / 1800.0);
                if (theta_max > M_PI/2.0) theta_max = M_PI/2.0;

                const double half_w = w / 2.0;
                const double half_h = h / 2.0;
                const double Rmax = sqrt(half_w*half_w + half_h*half_h);

                double theta_crit_x = atan2(half_h, half_w);
                double theta_crit_y = atan2(half_w, half_h);

                double x_ext_max, y_ext_max;
                if (theta_max >= theta_crit_x)
                    x_ext_max = Rmax;
                else
                    x_ext_max = half_w * cos(theta_max) + half_h * sin(theta_max);

                if (theta_max >= theta_crit_y)
                    y_ext_max = Rmax;
                else
                    y_ext_max = half_w * sin(theta_max) + half_h * cos(theta_max);

                x_ext_max = std::max(x_ext_max, half_w);
                y_ext_max = std::max(y_ext_max, half_h);

                double inv_scale_x = (half_w - dx_max) / x_ext_max;
                double inv_scale_y = (half_h - dy_max) / y_ext_max;
                double inv_scale = std::min(inv_scale_x, inv_scale_y);

                if (inv_scale <= 0.0) inv_scale = 0.5;
                double S = 1.0 / inv_scale;
                if (S < 1.0) S = 1.0;
                inst->cached_zoom = S;
            } else {
                // auto_zoom off: use manual zoom slider (scaled to factor)
                inst->cached_zoom = inst->params[6] / 100.0;
            }
            inst->zoom_dirty = false;
        }

        double effective_zoom = inst->cached_zoom; // either auto or manual

        // ---- Edge smoothing (padding) ----
        bool enable_padding = (inst->params[8] > 0.5);
        const int padding = enable_padding ? (int)(4.0 * scale + 0.5) : 0;

        const int pw = w + padding * 2;
        const int ph = h + padding * 2;
        uint32_t* padded = nullptr;
        const uint32_t* source_buf = inframe;
        int source_w = w;
        int source_h = h;

        if (padding > 0) {
            padded = new uint32_t[pw * ph];
            std::fill(padded, padded + pw * ph, 0u);
            for (int y = 0; y < h; ++y) {
                std::copy(inframe + y * w, inframe + (y + 1) * w,
                          padded + (y + padding) * pw + padding);
            }
            source_buf = padded;
            source_w = pw;
            source_h = ph;
        }

        calculate_content_bounds(inst, source_buf, source_w, source_h);

        // ---- Speed modulation (unchanged) ----
        const double speed_factor = inst->params[4] / 25.0;
        double speed_noise_percent = std::max(0.0, std::min(100.0, inst->params[5]));
        double bias_percent = std::max(-100.0, std::min(100.0, (double)SPEED_BIAS_PERCENT));
        double half_range = (speed_noise_percent / 100.0) * 0.5;
        double shift = half_range * (bias_percent / 100.0);
        double phase_t = time * speed_factor * 0.33 + inst->params[9] * 0.1; // the fluctuation tempo = 0.nn% of the main speed
        const double NOISE_PEAK = 1.28;
        double base_t = time * speed_factor * (1.0 + shift) + inst->params[9];
        double integral_term = (half_range / (NOISE_PEAK * 0.33)) * integral_organic_noise(phase_t); // the fluctuation tempo = 0.nn% of the main speed
        double final_t = base_t + integral_term;

        // ---- Shake & rotation ----
        const double shake_x = organic_noise(final_t) * inst->params[0] * scale;
        const double shake_y = organic_noise(final_t * 1.13) * inst->params[1] * scale;
        const double max_angle = inst->params[2] * (M_PI / 1800.0);
        const double theta = organic_noise(final_t * 0.72) * max_angle;
        const double cos_t = cos(-theta);
        const double sin_t = sin(-theta);

        int blur_radius = compute_blur_radius(inst, final_t);

        // Use the cached effective zoom
        double scale_zoom = effective_zoom;
        if (scale_zoom < 0.001) scale_zoom = 0.001;
        const double inv_scale = 1.0 / scale_zoom;

        const double cx = w / 2.0;
        const double cy = h / 2.0;

        const double a = cos_t * inv_scale;
        const double b = -sin_t * inv_scale;
        const double c = sin_t * inv_scale;
        const double d = cos_t * inv_scale;

        const double tx = cx + shake_x - (cx * a + cy * b);
        const double ty = cy + shake_y - (cx * c + cy * d);

        int min_x = std::max(0, inst->content_min_x - padding);
        int min_y = std::max(0, inst->content_min_y - padding);
        int max_x = std::min(source_w - 1, inst->content_max_x + padding);
        int max_y = std::min(source_h - 1, inst->content_max_y + padding);

        // ---- Render ----
        for (int y = 0; y < h; ++y) {
            const double base_x = b * (double)y + tx;
            const double base_y = d * (double)y + ty;
            for (int x = 0; x < w; ++x) {
                double src_x = a * (double)x + base_x + padding;
                double src_y = c * (double)x + base_y + padding;
                uint8_t* out_ptr = (uint8_t*)&outframe[y * w + x];

                if (blur_radius <= 0) {
                    content_clamped_smoothed_sample(source_buf, source_w, source_h,
                                                    min_x, min_y, max_x, max_y, src_x, src_y, out_ptr);
                } else {
                    if (src_x < inst->content_min_x - 1.0 || src_x > inst->content_max_x + 1.0 ||
                        src_y < inst->content_min_y - 1.0 || src_y > inst->content_max_y + 1.0) {
                        out_ptr[0] = out_ptr[1] = out_ptr[2] = out_ptr[3] = 0;
                        continue;
                    }
                    int ix = (int)(src_x + 0.5);
                    int iy = (int)(src_y + 0.5);
                    ix = std::max(min_x, std::min(ix, max_x));
                    iy = std::max(min_y, std::min(iy, max_y));
                    fast_blur_sample(source_buf, source_w, source_h,
                                     min_x, min_y, max_x, max_y,
                                     ix, iy, blur_radius, out_ptr);
                }
            }
        }

        if (padded) delete[] padded;
    }
}
