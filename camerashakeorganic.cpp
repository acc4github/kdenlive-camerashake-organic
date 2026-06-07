#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {
    #include <frei0r.h>

    /* Plugin instance data - holds ALL parameters and internal state */
    struct CameraShakeInstance {
        double amp_x, amp_y, rotation, zoom, speed, blur, seed, speed_noise;
        unsigned int width, height;
        
        /* Time tracking for variable speed control */
        double last_time;      // Real video time from the PREVIOUS frame
        double effective_t;    // Our custom internal clock (can slow down or slightly speed up)
    };

    int f0r_init(void) { return 1; }
    void f0r_deinit(void) {}

    /* Plugin metadata shown in Kdenlive */
    void f0r_get_plugin_info(f0r_plugin_info_t* info) {
        info->name = "Camera Shake Organic";
        info->author = "Modified by acc4commissions and Grok 4.3";
        info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
        info->color_model = F0R_COLOR_MODEL_RGBA8888;
        info->frei0r_version = FREI0R_MAJOR_VERSION;
        info->major_version = 2; 
        info->minor_version = 0;
        info->num_params = 8;
        info->explanation = "Organic camera shake with irregular noise, bilinear sampling, rotation, and natural speed noise variation (mostly slowdown with occasional small speedups).";
    }

    /* Parameter definitions - the order here MUST match your .xml UI file exactly */
    void f0r_get_param_info(f0r_param_info_t* info, int param_index) {
        if (param_index == 0) { info->name = "amplitude_x"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 1) { info->name = "amplitude_y"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 2) { info->name = "rotation"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 3) { info->name = "blur"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 4) { info->name = "speed"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 5) { info->name = "speed_noise"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 6) { info->name = "zoom"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 7) { info->name = "seed"; info->type = F0R_PARAM_DOUBLE; }
    }

    /* Create and initialize a new plugin instance with default values */
    f0r_instance_t f0r_construct(unsigned int width, unsigned int height) {
        CameraShakeInstance* inst = new CameraShakeInstance();
        inst->amp_x = 50.0; inst->amp_y = 50.0; inst->rotation = 10.0;
        inst->zoom = 100.0; inst->speed = 50.0; inst->blur = 0.0; 
        inst->seed = 0.0; inst->speed_noise = 0.0;
        inst->width = width; inst->height = height;
        inst->last_time = 0.0;
        inst->effective_t = 0.0;
        return (f0r_instance_t)inst;
    }

    void f0r_destruct(f0r_instance_t instance) {
        delete (CameraShakeInstance*)instance;
    }

    /* Update a parameter value coming from Kdenlive */
    void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        double val = *((double*)param);
        if (param_index == 0) inst->amp_x = val;
        else if (param_index == 1) inst->amp_y = val;
        else if (param_index == 2) inst->rotation = val;
        else if (param_index == 3) inst->blur = val;
        else if (param_index == 4) inst->speed = val;
        else if (param_index == 5) inst->speed_noise = val;
        else if (param_index == 6) inst->zoom = val;
        else if (param_index == 7) inst->seed = val;
    }

    /* Return current value of a parameter to Kdenlive */
    void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        if (param_index == 0) *((double*)param) = inst->amp_x;
        else if (param_index == 1) *((double*)param) = inst->amp_y;
        else if (param_index == 2) *((double*)param) = inst->rotation;
        else if (param_index == 3) *((double*)param) = inst->blur;
        else if (param_index == 4) *((double*)param) = inst->speed;
        else if (param_index == 5) *((double*)param) = inst->speed_noise;
        else if (param_index == 6) *((double*)param) = inst->zoom;
        else if (param_index == 7) *((double*)param) = inst->seed;
    }

    /* Organic irregular noise using layered sine waves for natural, non-repeating motion */
    double organic_noise(double t) {
        double n = sin(t) * 0.5 + sin(t * 2.31) * 0.25 +
                   sin(t * 4.57) * 0.15 + sin(t * 7.13) * 0.08;
        n += sin(t * 0.37) * 0.3;
        return n;
    }

    /* Bilinear interpolation - smooth sampling at sub-pixel positions */
    inline void bilinear_sample(const uint32_t* inframe, int w, int h, double fx, double fy, uint8_t* out) {
        int x0 = (int)fx;
        int y0 = (int)fy;
        if (x0 < 0 || x0 >= w-1 || y0 < 0 || y0 >= h-1) {
            out[0] = out[1] = out[2] = out[3] = 0;
            return;
        }

        double dx = fx - x0;
        double dy = fy - y0;

        const uint8_t* p00 = (const uint8_t*)&inframe[y0 * w + x0];
        const uint8_t* p10 = (const uint8_t*)&inframe[y0 * w + x0 + 1];
        const uint8_t* p01 = (const uint8_t*)&inframe[(y0 + 1) * w + x0];
        const uint8_t* p11 = (const uint8_t*)&inframe[(y0 + 1) * w + x0 + 1];

        double w00 = (1.0 - dx) * (1.0 - dy);
        double w10 = dx * (1.0 - dy);
        double w01 = (1.0 - dx) * dy;
        double w11 = dx * dy;

        out[0] = (uint8_t)(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11);
        out[1] = (uint8_t)(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11);
        out[2] = (uint8_t)(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11);
        out[3] = (uint8_t)(p00[3]*w00 + p10[3]*w10 + p01[3]*w01 + p11[3]*w11);
    }

    /* Main processing function - runs for every frame */
    void f0r_update(f0r_instance_t instance, double time, const uint32_t* inframe, uint32_t* outframe) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        const int w = inst->width;
        const int h = inst->height;

        const double speed_factor = inst->speed / 25.0;
        
        /* Calculate real time passed since the last frame */
        const double dt = time - inst->last_time;
        inst->last_time = time;   // Remember current time for next frame

        /* ====================== SPEED NOISE LOGIC ====================== */
        /* Controls occasional slowdowns + small speedups using separate noise */
        double slowdown_amount = 0.0;
        if (inst->speed_noise > 0.001) {
            /* Noise runs at 0.55 speed of the main speed */
            double slow_noise = organic_noise(time * speed_factor * 0.55 + inst->seed * 0.1);
            
            /* Scale noise → allows ~20% upward peaks while mostly slowing down */
            double normalized = (slow_noise + 1.20) / 2.40 * 1.2 - 0.2;
            
            /* Apply the slider strength (0-100%) */
            slowdown_amount = (inst->speed_noise / 100.0) * normalized;
        }
        /* ================================================================ */

        /* Advance our internal clock using the speed noise adjustment */
        double time_step = dt * speed_factor * (1.0 - slowdown_amount);
        inst->effective_t += time_step;

        /* This is the time used for ALL motion (shake, rotation, blur) */
        const double t = inst->effective_t + inst->seed;

        /* Compute shake offsets using organic noise */
        const double shake_x = organic_noise(t) * (inst->amp_x / 5.0);
        const double shake_y = organic_noise(t * 1.13) * (inst->amp_y / 5.0);

        /* Rotation calculation */
        const double max_angle = inst->rotation * (M_PI / 1800.0);
        const double theta = organic_noise(t * 0.72) * max_angle;
        const double cos_t = cos(-theta);
        const double sin_t = sin(-theta);

        /* Blur with its own fluctuation rate */
        const double blur_noise = organic_noise(t * 0.55);
        const double blur_factor = blur_noise * blur_noise * 0.6;
        const int blur_radius = (int)((inst->blur / 5.0) * blur_factor);

        /* Zoom handling */
        double scale = inst->zoom / 100.0;
        if (scale < 0.001) scale = 0.001;

        const double cx = w / 2.0;
        const double cy = h / 2.0;

        /* Precomputed affine transform for position + rotation + zoom */
        const double inv_scale = 1.0 / scale;
        const double a = cos_t * inv_scale;
        const double b = -sin_t * inv_scale;
        const double c = sin_t * inv_scale;
        const double d = cos_t * inv_scale;
        const double tx = cx + shake_x - (cx * a + cy * b);
        const double ty = cy + shake_y - (cx * c + cy * d);

        /* Render the transformed frame with optional blur */
        for (int y = 0; y < h; ++y) {
            const double base_x = b * (double)y + tx;
            const double base_y = d * (double)y + ty;

            for (int x = 0; x < w; ++x) {
                double src_x = a * (double)x + base_x;
                double src_y = c * (double)x + base_y;

                uint8_t* out_ptr = (uint8_t*)&outframe[y * w + x];

                if (blur_radius <= 0) {
                    bilinear_sample(inframe, w, h, src_x, src_y, out_ptr);
                } else {
                    int ix = (int)(src_x + 0.5);
                    int iy = (int)(src_y + 0.5);

                    if (ix < 0 || ix >= w || iy < 0 || iy >= h) {
                        out_ptr[0] = out_ptr[1] = out_ptr[2] = out_ptr[3] = 0;
                        continue;
                    }

                    int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
                    int samples = 0;

                    /* Horizontal blur samples */
                    for (int i = -blur_radius; i <= blur_radius; i += 2) {
                        int sx = ix + i;
                        if (sx >= 0 && sx < w) {
                            const uint8_t* p = (const uint8_t*)&inframe[iy * w + sx];
                            sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                            samples++;
                        }
                    }

                    /* Vertical blur samples */
                    for (int i = -blur_radius; i <= blur_radius; i += 2) {
                        if (i == 0) continue;
                        int sy = iy + i;
                        if (sy >= 0 && sy < h) {
                            const uint8_t* p = (const uint8_t*)&inframe[sy * w + ix];
                            sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                            samples++;
                        }
                    }

                    if (samples > 0) {
                        out_ptr[0] = sum_r / samples;
                        out_ptr[1] = sum_g / samples;
                        out_ptr[2] = sum_b / samples;
                        out_ptr[3] = sum_a / samples;
                    } else {
                        bilinear_sample(inframe, w, h, src_x, src_y, out_ptr);
                    }
                }
            }
        }
    }
}