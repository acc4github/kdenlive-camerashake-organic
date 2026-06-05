#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {
    #include <frei0r.h>

    struct CameraShakeInstance {
        double amp_x, amp_y, rotation, zoom, speed, blur;
        unsigned int width, height;
    };

    int f0r_init(void) { return 1; }
    void f0r_deinit(void) {}

    void f0r_get_plugin_info(f0r_plugin_info_t* info) {
        info->name = "Camera Shake Organic";
        info->author = "Modified for you";
        info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
        info->color_model = F0R_COLOR_MODEL_RGBA8888;
        info->frei0r_version = FREI0R_MAJOR_VERSION;
        info->major_version = 2; info->minor_version = 0;
        info->num_params = 6;
        info->explanation = "Organic camera shake with irregular noise.";
    }

    void f0r_get_param_info(f0r_param_info_t* info, int param_index) {
        if (param_index == 0) { info->name = "amplitude_x"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 1) { info->name = "amplitude_y"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 2) { info->name = "rotation"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 3) { info->name = "zoom"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 4) { info->name = "speed"; info->type = F0R_PARAM_DOUBLE; }
        else if (param_index == 5) { info->name = "blur"; info->type = F0R_PARAM_DOUBLE; }
    }

    f0r_instance_t f0r_construct(unsigned int width, unsigned int height) {
        CameraShakeInstance* inst = new CameraShakeInstance();
        inst->amp_x = 50.0; inst->amp_y = 50.0; inst->rotation = 10.0;
        inst->zoom = 100.0; inst->speed = 50.0; inst->blur = 0.0;
        inst->width = width; inst->height = height;
        return (f0r_instance_t)inst;
    }

    void f0r_destruct(f0r_instance_t instance) {
        delete (CameraShakeInstance*)instance;
    }

    void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        double val = *((double*)param);
        if (param_index == 0) inst->amp_x = val;
        else if (param_index == 1) inst->amp_y = val;
        else if (param_index == 2) inst->rotation = val;
        else if (param_index == 3) inst->zoom = val;
        else if (param_index == 4) inst->speed = val;
        else if (param_index == 5) inst->blur = val;
    }

    void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int param_index) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        if (param_index == 0) *((double*)param) = inst->amp_x;
        else if (param_index == 1) *((double*)param) = inst->amp_y;
        else if (param_index == 2) *((double*)param) = inst->rotation;
        else if (param_index == 3) *((double*)param) = inst->zoom;
        else if (param_index == 4) *((double*)param) = inst->speed;
        else if (param_index == 5) *((double*)param) = inst->blur;
    }

    double organic_noise(double t) {
        double n = sin(t) * 0.5 +
                   sin(t * 2.31) * 0.25 +
                   sin(t * 4.57) * 0.15 +
                   sin(t * 7.13) * 0.08;
        n += sin(t * 0.37) * 0.3;
        return n;
    }

    void f0r_update(f0r_instance_t instance, double time, const uint32_t* inframe, uint32_t* outframe) {
        CameraShakeInstance* inst = (CameraShakeInstance*)instance;
        int w = inst->width;
        int h = inst->height;

        double speed = inst->speed / 25.0;

        double shake_x = organic_noise(time * speed * 1.0) * inst->amp_x;
        double shake_y = organic_noise(time * speed * 1.13) * inst->amp_y;

        double max_angle = (inst->rotation / 100.0) * (M_PI / 4.0);
        double theta = organic_noise(time * speed * 0.72) * max_angle;

        double cos_t = cos(-theta);
        double sin_t = sin(-theta);

        double blur_noise = organic_noise(time * speed * 0.55);
        double blur_factor = blur_noise * blur_noise * 0.6;
        int blur_radius = (int)((inst->blur / 5.0) * blur_factor);

        double scale = inst->zoom / 100.0;
        if (scale < 0.001) scale = 0.001;

        double cx = w / 2.0;
        double cy = h / 2.0;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double dx = (x - cx) / scale;
                double dy = (y - cy) / scale;

                double rx = dx * cos_t - dy * sin_t;
                double ry = dx * sin_t + dy * cos_t;

                int src_x = (int)(cx + rx + shake_x + 0.5);
                int src_y = (int)(cy + ry + shake_y + 0.5);

                uint8_t* out_ptr = (uint8_t*)&outframe[y * w + x];

                if (src_x < 0 || src_x >= w || src_y < 0 || src_y >= h) {
                    out_ptr[0] = 0;
                    out_ptr[1] = 0;
                    out_ptr[2] = 0;
                    out_ptr[3] = 0;
                    continue;
                }

                uint8_t* in_ptr = (uint8_t*)&inframe[src_y * w + src_x];

                if (blur_radius <= 0) {
                    out_ptr[0] = in_ptr[0];
                    out_ptr[1] = in_ptr[1];
                    out_ptr[2] = in_ptr[2];
                    out_ptr[3] = in_ptr[3];
                } else {
                    int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
                    int samples = 0;

                    for (int i = -blur_radius; i <= blur_radius; i += 2) {
                        int sx = src_x + i;
                        if (sx >= 0 && sx < w) {
                            uint8_t* p = (uint8_t*)&inframe[src_y * w + sx];
                            sum_r += p[0]; sum_g += p[1]; sum_b += p[2]; sum_a += p[3];
                            samples++;
                        }
                        int sy = src_y + i;
                        if (sy >= 0 && sy < h && i != 0) {
                            uint8_t* p = (uint8_t*)&inframe[sy * w + src_x];
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
                        out_ptr[0] = in_ptr[0];
                        out_ptr[1] = in_ptr[1];
                        out_ptr[2] = in_ptr[2];
                        out_ptr[3] = in_ptr[3];
                    }
                }
            }
        }
    }
}