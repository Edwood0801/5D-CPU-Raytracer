#include <SDL3/SDL.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <omp.h>

// ==========================================
// 1. FAST MATH & PHYSICS
// ==========================================
typedef struct { float x, y, z; } Vec3;

Vec3 vec_add(Vec3 a, Vec3 b) { return (Vec3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 vec_sub(Vec3 a, Vec3 b) { return (Vec3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 vec_mul(Vec3 a, float t) { return (Vec3) { a.x* t, a.y* t, a.z* t }; }
float vec_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float vec_length_squared(Vec3 a) { return vec_dot(a, a); }
float vec_length(Vec3 a) { return sqrt(vec_length_squared(a)); }
Vec3 vec_normalize(Vec3 a) { return vec_mul(a, 1.0f / vec_length(a)); }
Vec3 vec_cross(Vec3 a, Vec3 b) {
    return (Vec3) { a.y* b.z - a.z * b.y, a.z* b.x - a.x * b.z, a.x* b.y - a.y * b.x };
}

typedef struct { Vec3 origin; Vec3 direction; } Ray;
Vec3 ray_at(Ray r, float t) { return vec_add(r.origin, vec_mul(r.direction, t)); }

typedef struct {
    Vec3 point;
    Vec3 normal;
    float t;
    bool front_face;
    int object_index;
} HitRecord;

void set_face_normal(HitRecord* rec, Ray r, Vec3 outward_normal) {
    rec->front_face = vec_dot(r.direction, outward_normal) < 0;
    rec->normal = rec->front_face ? outward_normal : vec_mul(outward_normal, -1.0f);
}

typedef struct {
    Vec3 center;
    float radius;
    Vec3 color;
    int material_type;
} Sphere;

bool hit_sphere(Sphere s, Ray r, float t_min, float t_max, HitRecord* rec) {
    Vec3 oc = vec_sub(r.origin, s.center);
    float a = vec_length_squared(r.direction);
    float half_b = vec_dot(oc, r.direction);
    float c = vec_length_squared(oc) - s.radius * s.radius;

    float discriminant = half_b * half_b - a * c;
    if (discriminant < 0) return false;

    float sqrtd = sqrt(discriminant);
    float root = (-half_b - sqrtd) / a;
    if (root <= t_min || root >= t_max) {
        root = (-half_b + sqrtd) / a;
        if (root <= t_min || root >= t_max) return false;
    }

    rec->t = root;
    rec->point = ray_at(r, rec->t);
    Vec3 outward_normal = vec_mul(vec_sub(rec->point, s.center), 1.0f / s.radius);
    set_face_normal(rec, r, outward_normal);
    return true;
}

// ==========================================
// 2. THE NEW TOON SHADER (CARTOON LIGHTING)
// ==========================================
Vec3 ray_color(Ray r, Sphere* world, int object_count, Vec3 light_pos) {
    HitRecord rec;
    bool hit_anything = false;
    float closest_so_far = 100000.0f;
    HitRecord temp_rec;

    for (int i = 0; i < object_count; i++) {
        if (hit_sphere(world[i], r, 0.001f, closest_so_far, &temp_rec)) {
            hit_anything = true;
            closest_so_far = temp_rec.t;
            temp_rec.object_index = i;
            rec = temp_rec;
        }
    }

    if (hit_anything) {
        Sphere hit_obj = world[rec.object_index];

        // 1. Get the angle of the light
        Vec3 light_dir = vec_normalize(vec_sub(light_pos, rec.point));
        float diffuse_intensity = vec_dot(rec.normal, light_dir);

        // 2. CARTOON SHADING MAGIC (No expensive shadow rays!)
        float final_light;
        if (diffuse_intensity > 0.6f) {
            final_light = 1.0f;       // Physics Diagram Bright Spot
        }
        else if (diffuse_intensity > 0.1f) {
            final_light = 0.6f;       // Mid-tone color band
        }
        else {
            final_light = 0.2f;       // Hard dark shadow
        }

        return vec_mul(hit_obj.color, final_light);
    }

    // Background Sky Gradient
    Vec3 unit_direction = vec_normalize(r.direction);
    float t = 0.5f * (unit_direction.y + 1.0f);
    Vec3 white = { 1.0f, 1.0f, 1.0f };
    Vec3 blue = { 0.05f, 0.05f, 0.1f };
    return vec_add(vec_mul(white, 1.0f - t), vec_mul(blue, t));
}

// ==========================================
// 3. MAIN APPLICATION LOOP
// ==========================================
int main(int argc, char* argv[]) {
    const int window_width = 1280;
    const int window_height = 720;

    // Resolution doubled to make it smoother! (We can afford it now)
    const int image_width = 320;
    const int image_height = 180;

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;

    SDL_Window* window = SDL_CreateWindow("v3.0 Toon Shader Engine | WASD", window_width, window_height, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, image_width, image_height);

    // Keeping the crisp scaling so it looks like a retro anime/diagram
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    uint32_t* pixels = (uint32_t*)malloc(image_width * image_height * sizeof(uint32_t));

    Vec3 camera_origin = { 0.0f, 0.5f, 2.5f };
    Vec3 camera_lookat = { 0.0f, 0.0f, -0.5f };
    Vec3 vup = { 0.0f, 1.0f, 0.0f };
    Vec3 light_pos = { 0.0f, 0.0f, 1.0f };

    float aspect_ratio = (float)image_width / image_height;
    float viewport_height = 2.0f;
    float viewport_width = aspect_ratio * viewport_height;

    Sphere world[2] = {
        { {0.0f, 0.0f, -0.5f}, 0.6f, {1.0f, 0.3f, 0.3f}, 0 },
        { {0.0f, -100.6f, -0.5f}, 100.0f, {0.9f, 0.9f, 0.9f}, 0 }
    };
    int object_count = 2;

    bool quit = false;
    SDL_Event e;

    float inv_width = 1.0f / (image_width - 1);
    float inv_height = 1.0f / (image_height - 1);

    uint64_t last_time = SDL_GetTicks();
    int frame_count = 0;

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) quit = true;
        }

        const bool* state = SDL_GetKeyboardState(NULL);
        float move_speed = 0.1f;
        if (state[SDL_SCANCODE_W]) camera_origin.z -= move_speed;
        if (state[SDL_SCANCODE_S]) camera_origin.z += move_speed;
        if (state[SDL_SCANCODE_A]) camera_origin.x -= move_speed;
        if (state[SDL_SCANCODE_D]) camera_origin.x += move_speed;
        if (state[SDL_SCANCODE_E]) camera_origin.y += move_speed;
        if (state[SDL_SCANCODE_Q]) camera_origin.y -= move_speed;

        float mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        light_pos.x = camera_origin.x + ((mouse_x / window_width) * viewport_width - (viewport_width * 0.5f)) * 2.0f;
        light_pos.y = camera_origin.y - ((mouse_y / window_height) * viewport_height - (viewport_height * 0.5f)) * 2.0f;
        light_pos.z = camera_origin.z - 1.0f;

        Vec3 w = vec_normalize(vec_sub(camera_origin, camera_lookat));
        Vec3 u = vec_normalize(vec_cross(vup, w));
        Vec3 v = vec_cross(w, u);

        Vec3 horizontal = vec_mul(u, viewport_width);
        Vec3 vertical = vec_mul(v, viewport_height);
        Vec3 lower_left_corner = vec_sub(camera_origin, vec_mul(horizontal, 0.5f));
        lower_left_corner = vec_sub(lower_left_corner, vec_mul(vertical, 0.5f));
        lower_left_corner = vec_sub(lower_left_corner, w);

#pragma omp parallel for schedule(dynamic)
        for (int j = 0; j < image_height; ++j) {
            for (int i = 0; i < image_width; ++i) {
                float u_coord = (float)i * inv_width;
                float v_coord = (float)(image_height - 1 - j) * inv_height;

                Ray r = { camera_origin, vec_sub(vec_add(vec_add(lower_left_corner, vec_mul(horizontal, u_coord)), vec_mul(vertical, v_coord)), camera_origin) };

                // Calling the new super-fast Toon Shader
                Vec3 pixel_color = ray_color(r, world, object_count, light_pos);

                float r_c = pixel_color.x > 1.0f ? 1.0f : pixel_color.x;
                float g_c = pixel_color.y > 1.0f ? 1.0f : pixel_color.y;
                float b_c = pixel_color.z > 1.0f ? 1.0f : pixel_color.z;

                int ir = (int)(255.999f * r_c);
                int ig = (int)(255.999f * g_c);
                int ib = (int)(255.999f * b_c);

                pixels[j * image_width + i] = (ir << 16) | (ig << 8) | ib;
            }
        }

        SDL_UpdateTexture(texture, NULL, pixels, image_width * sizeof(uint32_t));
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frame_count++;
        uint64_t current_time = SDL_GetTicks();
        if (current_time - last_time >= 500) {
            float fps = frame_count / ((current_time - last_time) / 1000.0f);
            char title[128];
            snprintf(title, sizeof(title), "v3.0 Toon Shader Engine | Smooth Res | FPS: %.1f", fps);
            SDL_SetWindowTitle(window, title);
            last_time = current_time;
            frame_count = 0;
        }
    }

    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}