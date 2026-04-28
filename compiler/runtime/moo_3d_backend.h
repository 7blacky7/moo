#ifndef MOO_3D_BACKEND_H
#define MOO_3D_BACKEND_H

/**
 * moo_3d_backend.h — Backend-Interface fuer Multi-Engine 3D-Rendering.
 * Jedes Backend implementiert diesen Funktionszeiger-Struct.
 */

typedef struct {
    // Lifecycle
    void* (*create_window)(const char* title, int w, int h);
    void  (*close)(void* ctx);
    int   (*is_open)(void* ctx);
    // Frame
    void  (*clear)(void* ctx, float r, float g, float b);
    void  (*swap)(void* ctx);
    // Kamera
    void  (*perspective)(void* ctx, float fov, float near, float far);
    void  (*camera)(void* ctx, float ex, float ey, float ez, float lx, float ly, float lz);
    // Transform
    void  (*push_matrix)(void* ctx);
    void  (*pop_matrix)(void* ctx);
    void  (*translate)(void* ctx, float x, float y, float z);
    void  (*rotate)(void* ctx, float angle, float ax, float ay, float az);
    // Zeichnen
    void  (*cube)(void* ctx, float x, float y, float z, float size, float r, float g, float b);
    void  (*sphere)(void* ctx, float x, float y, float z, float radius, float r, float g, float b, int detail);
    void  (*triangle)(void* ctx, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, float r, float g, float b);
    int   (*key_pressed)(void* ctx, const char* key);
    // Maus
    void  (*capture_mouse)(void* ctx);
    void  (*release_mouse)(void* ctx);
    float (*mouse_dx)(void* ctx);
    float (*mouse_dy)(void* ctx);
    float (*mouse_x)(void* ctx);
    float (*mouse_y)(void* ctx);
    int   (*mouse_button)(void* ctx, int btn); // 0=LMB, 1=RMB, 2=MMB
    float (*mouse_wheel)(void* ctx);           // accumulated scroll-y, consume-on-read
    // Fog + Licht
    void  (*set_fog_density)(void* ctx, float density);
    void  (*set_light_dir)(void* ctx, float x, float y, float z);
    void  (*set_ambient)(void* ctx, float level);
    // Chunks
    int   (*chunk_create)(void* ctx);
    void  (*chunk_begin)(void* ctx, int id);
    void  (*chunk_end)(void* ctx);
    void  (*chunk_draw)(void* ctx, int id);
    void  (*chunk_delete)(void* ctx, int id);
    // Screenshot — schreibt aktuelles Frame-Buffer als BMP. Returns 1 bei Erfolg, 0 sonst.
    int   (*screenshot_bmp)(void* ctx, const char* path);
    // Test-Sim: programmatische Eingaben fuer Selbsttests
    void  (*simulate_mouse_pos)(void* ctx, float x, float y);
    void  (*simulate_mouse_button)(void* ctx, int btn, int pressed); // btn 0/1/2, pressed 0/1
    void  (*simulate_scroll)(void* ctx, float dy);
} Moo3DBackend;

// Backend-Deklarationen (je nach Feature-Flag verfuegbar)
extern Moo3DBackend moo_backend_gl21;
extern Moo3DBackend moo_backend_gl33;
extern Moo3DBackend moo_backend_vulkan;

#endif
