#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

extern "C" {
    #include <frei0r.h>

    /* =============================================
       PLUGIN INSTANCE DATA
       ============================================= */

    // Holds per-instance state for the Camera Shake Organic effect
    struct CameraShakeInstance {
        double params[9];           // Parameter storage (matches XML order)
                                    // 0: amplitude_x, 1: amplitude_y, 2: rotation,
                                    // 3: blur, 4: speed, 5: speed_noise,
                                    // 6: zoom, 7: edge_smoothing (bool as double),
                                    // 8: seed / phase

        unsigned int width, height; // Frame dimensions

        double last_time;           // Previous frame time (for delta calculation)
        double effective_t;         // Accumulated animated time (with slowdowns)

        // Tight bounding box of non-transparent content (for efficient sampling)
        int content_min_x, content_min_y;
        int content_max_x, content_max_y;
        bool bounds_calculated;     // Cache flag
    };

    // Frei0r plugin lifecycle
    int f0r_init(void) { return 1; }
    void f0r_deinit(void) {}

    // Plugin metadata exposed to Kdenlive / Frei0r host
    void f0r_get_plugin_info(f0r_plugin_info_t* info) {
        info->name = "Camera Shake Organic (Edge Smoothing)";
        info->author = "Modified by acc4commissions and Grok 4.3";
        info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
        info->color_model = F0R_COLOR_MODEL_RGBA8888;
        info->frei0r_version = FREI0R_MAJOR_VERSION;
        info->major_version = 2;
        info->minor_version = 0;
        info->num_params = 9;
        info->explanation = "Organic irregular camera shake with noise on movement, rotation, blur, zoom and occasional speed slowdown. Optional edge smoothing padding.";
    }

    /* Parameter metadata (must match XML definition order) */
    void f0r_get_param_info(f0r_param_info_t* info, int param_index) {
        static const char* names[9] = {
            "amplitude_x", "amplitude_y", "rotation", "blur",
            "speed", "speed_noise", "zoom", "edge_smoothing", "seed"
        };
        info->name = names[param_index];
        info->type = (param_index == 7) ? F0R_PARAM_BOOL : F0R_PARAM_DOUBLE;
    }

    // Create new effect instance
    f0r_instance_t f0r_construct(unsigned int width, unsigned int height) {
        CameraShakeInstance* inst = new CameraShakeInstance();

        // Default parameter values
        inst->params[0] = 8.0;   // X Amplitude
        inst->params[1] = 8.0;   // Y Amplitude
        inst->params[2] = 2.0;   // Max Rotation
        inst->params[3] = 0.0;   // Blur
        inst->params[4] = 55.0;  // Speed
        inst->params[5] = 0.0;   // Speed Noise
        inst->params[6] = 100.0; // Zoom
        inst->params[7] = 0.0;   // Edge Smoothing = OFF
        inst->params[8] = 0.0;   // Seed / Phase

        inst->width = width;
        inst->height = height;
        inst->last_time = 0.0;
        inst->effective_t = 0.0;
        inst->bounds_calculated = false;

        // Initial full-frame bounds
        inst->content_min_x = 0;
        inst->content_max_x = (int)width - 1;
        inst->content_min_y = 0;
        inst->content_max_y = (int)height - 1;

        return (f0r_instance_t)inst;
    }

    void f0r_destruct(f0r_instance_t instance) {
        delete (CameraShakeInstance*)instance;
    }

    // Update a parameter value from the host
    void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        inst->params[param_index] = *((double*)param);

        // Reset bounds cache when zoom or seed changes (affects sampling area)
        if (param_index == 6 || param_index == 8) {
            inst->bounds_calculated = false;
        }
    }

    void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        *((double*)param) = ((CameraShakeInstance*)instance)->params[param_index];
    }

    /* =============================================
       HELPER FUNCTIONS
       ============================================= */

    // Organic noise generator: layered sine waves create natural, irregular motion
    // (more pleasing than pure Perlin or simple random)
    double organic_noise(double t) {
        double n = sin(t) * 0.5 +
                   sin(t * 2.31) * 0.25 +
                   sin(t * 4.57) * 0.15 +
                   sin(t * 7.13) * 0.08;
        n += sin(t * 0.37) * 0.3;
        return n;
    }

    // Detects the tight bounding rectangle of visible (non-transparent) content
    // Used to avoid sampling empty areas and keep shake tight to the subject
    void calculate_content_bounds(CameraShakeInstance* inst, const uint32_t* buf, int bw, int bh) {
        if (inst->bounds_calculated) return;

        int left = bw, right = -1, top = bh, bottom = -1;

        // Quick scan: middle row for horizontal bounds
        for (int x = 0; x < bw; ++x) {
            const uint8_t* p = (const uint8_t*)&buf[(bh/2) * bw + x];
            if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }

        // Middle column for vertical bounds
        for (int y = 0; y < bh; ++y) {
            const uint8_t* p = (const uint8_t*)&buf[y * bw + (bw/2)];
            if (p[0] > 0 || p[1] > 0 || p[2] > 0 || p[3] > 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }

        // Fallback to full frame if nothing detected
        if (left > right || top > bottom || left >= bw || right < 0) {
            left = 0; right = bw - 1; top = 0; bottom = bh - 1;
        }

        inst->content_min_x = left;
        inst->content_max_x = right;
        inst->content_min_y = top;
        inst->content_max_y = bottom;
        inst->bounds_calculated = true;
    }

    // Bilinear interpolation with safe clamping to content bounds
    inline void content_clamped_smoothed_sample(const uint32_t* buf, int bw, int bh,
                                                int min_x, int min_y, int max_x, int max_y,
                                                double fx, double fy, uint8_t* out) {
        // Outside safe area → fully transparent
        if (fx < min_x - 1.0 || fx > max_x + 1.0 || fy < min_y - 1.0 || fy > max_y + 1.0) {
            out[0] = out[1] = out[2] = out[3] = 0;
            return;
        }

        int x0 = (int)fx;
        int y0 = (int)fy;
        double dx = fx - x0;
        double dy = fy - y0;

        // Clamp to valid content region
        x0 = std::max(min_x, std::min(x0, max_x - 1));
        y0 = std::max(min_y, std::min(y0, max_y - 1));

        int x1 = std::min(x0 + 1, max_x);
        int y1 = std::min(y0 + 1, max_y);

        const uint8_t* p00 = (const uint8_t*)&buf[y0 * bw + x0];
        const uint8_t* p10 = (const uint8_t*)&buf[y0 * bw + x1];
        const uint8_t* p01 = (const uint8_t*)&buf[y1 * bw + x0];
        const uint8_t* p11 = (const uint8_t*)&buf[y1 * bw + x1];

        // Bilinear weights
        double w00 = (1.0 - dx) * (1.0 - dy);
        double w10 = dx * (1.0 - dy);
        double w01 = (1.0 - dx) * dy;
        double w11 = dx * dy;

        out[0] = (uint8_t)(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11 + 0.5);
        out[1] = (uint8_t)(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11 + 0.5);
        out[2] = (uint8_t)(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11 + 0.5);
        out[3] = (uint8_t)(p00[3]*w00 + p10[3]*w10 + p01[3]*w01 + p11[3]*w11 + 0.5);
    }

    // Fast approximate cross-shaped blur (horizontal + vertical lines)
    // Much faster than full Gaussian while still giving believable motion blur
    inline void fast_blur_sample(const uint32_t* buf, int bw, int bh,
                                 int min_x, int min_y, int max_x, int max_y,
                                 int ix, int iy, int blur_radius, uint8_t* out) {
        if (blur_radius <= 0) {
            content_clamped_smoothed_sample(buf, bw, bh, min_x, min_y, max_x, max_y, (double)ix, (double)iy, out);
            return;
        }

        int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        int samples = 0;

        const int step = (blur_radius > 3) ? 2 : 1;  // Performance: coarser sampling for larger blur

        // Horizontal pass
        for (int i = -blur_radius; i <= blur_radius; i += step) {
            int sx = ix + i;
            if (sx >= min_x && sx <= max_x) {
                const uint8_t* p = (const uint8_t*)&buf[iy * bw + sx];
                sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                samples++;
            }
        }

        // Vertical pass (skip center to avoid double-counting)
        for (int i = -blur_radius; i <= blur_radius; i += step) {
            if (i == 0) continue;
            int sy = iy + i;
            if (sy >= min_y && sy <= max_y) {
                const uint8_t* p = (const uint8_t*)&buf[sy * bw + ix];
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

    // Computes occasional slowdowns for more organic feel
    double compute_speed_slowdown(CameraShakeInstance* inst, double time, double speed_factor) {
        double slowdown = 0.0;
        if (inst->params[5] > 0.001) {
            double slow_noise = organic_noise(time * speed_factor * 0.60 + inst->params[8] * 0.1);
            double normalized = (slow_noise + 1.20) / 2.40 * 1.25 - 0.25;
            slowdown = (inst->params[5] / 100.0) * normalized;
        }
        return slowdown;
    }

    // Animated blur amount based on noise — stronger shakes get more blur
    // Hard-capped at 6px for performance
    int compute_blur_radius(CameraShakeInstance* inst, double t) {
        double blur_noise = organic_noise(t * 0.55);
        double blur_factor = blur_noise * blur_noise * 0.5;

        const double threshold = 0.5;
        const double compression = 0.5;
        if (blur_factor > threshold) {
            blur_factor = threshold + (blur_factor - threshold) * compression;
        }

        double adjusted = fmax(blur_factor - 0.05, 0.0);

        int radius = (int)((inst->params[3] / 5.0) * adjusted);
        return std::min(radius, 6);
    }

    /* =============================================
       MAIN PROCESSING FUNCTION (f0r_update)
       ============================================= */

    void f0r_update(f0r_instance_t instance, double time, const uint32_t* inframe, uint32_t* outframe) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        const int w = inst->width;
        const int h = inst->height;

        /* === EDGE SMOOTHING (PADDING) === */
        bool enable_padding = (inst->params[7] > 0.5);
        const int padding = enable_padding ? 2 : 0;   // Small 2px border when enabled

        const int pw = w + padding * 2;
        const int ph = h + padding * 2;

        uint32_t* padded = nullptr;
        const uint32_t* source_buf = inframe;
        int source_w = w;
        int source_h = h;

        // Build padded buffer only when needed (transparent borders)
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

        /* === TIME & MOTION CALCULATION === */
        const double speed_factor = inst->params[4] / 25.0;
        const double dt = time - inst->last_time;
        inst->last_time = time;

        double slowdown = compute_speed_slowdown(inst, time, speed_factor);
        double time_step = dt * speed_factor * (1.0 - slowdown);

        inst->effective_t += time_step;
        const double t = inst->effective_t + inst->params[8];  // Apply seed/phase

        // Translation shake
        const double shake_x = organic_noise(t) * (inst->params[0] / 5.0);
        const double shake_y = organic_noise(t * 1.13) * (inst->params[1] / 5.0);

        // Rotation
        const double max_angle = inst->params[2] * (M_PI / 1800.0);  // 0.1° units
        const double theta = organic_noise(t * 0.72) * max_angle;
        const double cos_t = cos(-theta);
        const double sin_t = sin(-theta);

        int blur_radius = compute_blur_radius(inst, t);

        // Zoom (scale around center)
        double scale = inst->params[6] / 100.0;
        if (scale < 0.001) scale = 0.001;
        const double inv_scale = 1.0 / scale;

        const double cx = w / 2.0;
        const double cy = h / 2.0;

        // Affine transform components
        const double a = cos_t * inv_scale;
        const double b = -sin_t * inv_scale;
        const double c = sin_t * inv_scale;
        const double d = cos_t * inv_scale;

        const double tx = cx + shake_x - (cx * a + cy * b);
        const double ty = cy + shake_y - (cx * c + cy * d);

        /* Sampling bounds (expanded only when padding is active) */
        int min_x = std::max(0, inst->content_min_x - padding);
        int min_y = std::max(0, inst->content_min_y - padding);
        int max_x = std::min(source_w - 1, inst->content_max_x + padding);
        int max_y = std::min(source_h - 1, inst->content_max_y + padding);

        /* === MAIN RENDER LOOP === */
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
                    // Strict guard: keep output inside original content bounds
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

        if (padded) delete[] padded;   // Cleanup
    }
}
