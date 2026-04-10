/**
 * moo_3d.c — OpenGL 3D Runtime fuer moo (Immediate Mode).
 * GLFW fuer Fenster, GLEW fuer Extension-Loading.
 */

#include "moo_runtime.h"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// === MooWindow3D Struktur ===
typedef struct {
    GLFWwindow* window;
    int width;
    int height;
} MooWindow3D;

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

// === Farb-Helfer ===
typedef struct { float r, g, b; } Color3;

static Color3 parse_color3(MooValue c) {
    Color3 col = {1.0f, 1.0f, 1.0f};
    if (c.tag != MOO_STRING) return col;
    const char* s = MV_STR(c)->chars;

    if (s[0] == '#' && strlen(s) == 7) {
        unsigned int hex;
        sscanf(s + 1, "%06x", &hex);
        col.r = ((hex >> 16) & 0xFF) / 255.0f;
        col.g = ((hex >> 8) & 0xFF) / 255.0f;
        col.b = (hex & 0xFF) / 255.0f;
        return col;
    }

    if (strcmp(s, "rot") == 0 || strcmp(s, "red") == 0) { col.r=1; col.g=0; col.b=0; }
    else if (strcmp(s, "gruen") == 0 || strcmp(s, "green") == 0 || strcmp(s, "grün") == 0) { col.r=0; col.g=1; col.b=0; }
    else if (strcmp(s, "blau") == 0 || strcmp(s, "blue") == 0) { col.r=0; col.g=0; col.b=1; }
    else if (strcmp(s, "weiss") == 0 || strcmp(s, "white") == 0 || strcmp(s, "weiß") == 0) { col.r=1; col.g=1; col.b=1; }
    else if (strcmp(s, "schwarz") == 0 || strcmp(s, "black") == 0) { col.r=0; col.g=0; col.b=0; }
    else if (strcmp(s, "gelb") == 0 || strcmp(s, "yellow") == 0) { col.r=1; col.g=1; col.b=0; }
    else if (strcmp(s, "cyan") == 0) { col.r=0; col.g=1; col.b=1; }
    else if (strcmp(s, "magenta") == 0) { col.r=1; col.g=0; col.b=1; }
    else if (strcmp(s, "orange") == 0) { col.r=1; col.g=0.65f; col.b=0; }
    else if (strcmp(s, "grau") == 0 || strcmp(s, "gray") == 0) { col.r=0.5f; col.g=0.5f; col.b=0.5f; }
    return col;
}

static MooWindow3D* get_win3d(MooValue v) {
    if (v.tag != MOO_WINDOW3D) return NULL;
    return (MooWindow3D*)moo_val_as_ptr(v);
}

// ============================================================
// Fenster
// ============================================================

static int glfw_initialized = 0;

MooValue moo_3d_create(MooValue title, MooValue w, MooValue h) {
    if (!glfw_initialized) {
        if (!glfwInit()) {
            moo_throw(moo_string_new("GLFW Init fehlgeschlagen"));
            return moo_none();
        }
        glfw_initialized = 1;
    }

    const char* t = (title.tag == MOO_STRING) ? MV_STR(title)->chars : "moo 3D";
    int width = (w.tag == MOO_NUMBER) ? (int)MV_NUM(w) : 800;
    int height = (h.tag == MOO_NUMBER) ? (int)MV_NUM(h) : 600;

    // OpenGL 2.1 Compatibility fuer Immediate Mode
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* win = glfwCreateWindow(width, height, t, NULL, NULL);
    if (!win) {
        moo_throw(moo_string_new("GLFW Fenster erstellen fehlgeschlagen"));
        return moo_none();
    }

    glfwMakeContextCurrent(win);

    // Kein GLEW noetig fuer Immediate Mode (GL 1.x/2.x Core)

    // WICHTIG: glViewport setzen!
    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // Standard-Licht
    float light_pos[] = {5.0f, 10.0f, 5.0f, 1.0f};
    float light_amb[] = {0.3f, 0.3f, 0.3f, 1.0f};
    float light_dif[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_dif);

    // Default-Perspektive setzen damit man sofort was sieht
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)width / (float)height;
    float fov_rad = 60.0f * (float)M_PI / 180.0f;
    float th = tanf(fov_rad / 2.0f);
    float proj[16] = {0};
    proj[0]  = 1.0f / (aspect * th);
    proj[5]  = 1.0f / th;
    proj[10] = -(100.0f + 0.1f) / (100.0f - 0.1f);
    proj[11] = -1.0f;
    proj[14] = -(2.0f * 100.0f * 0.1f) / (100.0f - 0.1f);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    MooWindow3D* mw = (MooWindow3D*)malloc(sizeof(MooWindow3D));
    mw->window = win;
    mw->width = width;
    mw->height = height;

    MooValue result;
    result.tag = MOO_WINDOW3D;
    moo_val_set_ptr(&result, mw);
    return result;
}

MooValue moo_3d_is_open(MooValue win) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return moo_bool(false);
    return moo_bool(!glfwWindowShouldClose(mw->window));
}

void moo_3d_clear(MooValue win, MooValue r, MooValue g, MooValue b) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return;
    glfwMakeContextCurrent(mw->window);
    glClearColor((float)MV_NUM(r), (float)MV_NUM(g), (float)MV_NUM(b), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // MODELVIEW zuruecksetzen — Kamera wird danach per moo_3d_camera() neu gesetzt
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void moo_3d_update(MooValue win) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return;
    glfwSwapBuffers(mw->window);
    glfwPollEvents();
}

void moo_3d_close(MooValue win) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return;
    glfwDestroyWindow(mw->window);
    mw->window = NULL;
}

// ============================================================
// Kamera & Projektion
// ============================================================

void moo_3d_perspective(MooValue win, MooValue fov, MooValue near_val, MooValue far_val) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return;
    glfwMakeContextCurrent(mw->window);

    float f = (float)MV_NUM(fov);
    float n = (float)MV_NUM(near_val);
    float fa = (float)MV_NUM(far_val);
    float aspect = (float)mw->width / (float)mw->height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Manuelle Perspective-Matrix (kein glu noetig)
    float rad = f * (float)M_PI / 180.0f;
    float tanHalf = tanf(rad / 2.0f);
    float m[16] = {0};
    m[0] = 1.0f / (aspect * tanHalf);
    m[5] = 1.0f / tanHalf;
    m[10] = -(fa + n) / (fa - n);
    m[11] = -1.0f;
    m[14] = -(2.0f * fa * n) / (fa - n);
    glLoadMatrixf(m);

    glMatrixMode(GL_MODELVIEW);
}

void moo_3d_camera(MooValue win, MooValue eyeX, MooValue eyeY, MooValue eyeZ,
                   MooValue lookX, MooValue lookY, MooValue lookZ) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw) return;
    glfwMakeContextCurrent(mw->window);

    float ex = (float)MV_NUM(eyeX), ey = (float)MV_NUM(eyeY), ez = (float)MV_NUM(eyeZ);
    float lx = (float)MV_NUM(lookX), ly = (float)MV_NUM(lookY), lz = (float)MV_NUM(lookZ);
    // LookAt-Matrix berechnen (Up = 0,1,0)
    // forward = normalize(look - eye)
    float fx = lx - ex, fy = ly - ey, fz = lz - ez;
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    if (len > 0) { fx /= len; fy /= len; fz /= len; }

    // side = normalize(cross(forward, up))
    // up = (0,1,0): cross(f, up) = (fy*0 - fz*1, fz*0 - fx*0, fx*1 - fy*0) = (-fz, 0, fx)
    float sx = -fz, sy = 0.0f, sz = fx;
    len = sqrtf(sx*sx + sz*sz);
    if (len > 0) { sx /= len; sz /= len; }

    // recompute up = cross(side, forward)
    float ux = sy*fz - sz*fy;
    float uy = sz*fx - sx*fz;
    float uz = sx*fy - sy*fx;

    // Translation direkt einbauen: dot(-eye, achse)
    float tx = -(sx*ex + sy*ey + sz*ez);
    float ty = -(ux*ex + uy*ey + uz*ez);
    float tz = -(-fx*ex + -fy*ey + -fz*ez);

    // OpenGL column-major: m[col*4 + row]
    // Spalte 0: (sx, ux, -fx, 0)
    // Spalte 1: (sy, uy, -fy, 0)
    // Spalte 2: (sz, uz, -fz, 0)
    // Spalte 3: (tx, ty, tz,  1)
    float m[16] = {
         sx,  ux, -fx, 0,
         sy,  uy, -fy, 0,
         sz,  uz, -fz, 0,
         tx,  ty,  tz, 1
    };

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

// ============================================================
// Transform
// ============================================================

void moo_3d_rotate(MooValue win, MooValue angle, MooValue ax, MooValue ay, MooValue az) {
    (void)win;
    glRotatef((float)MV_NUM(angle), (float)MV_NUM(ax), (float)MV_NUM(ay), (float)MV_NUM(az));
}

void moo_3d_translate(MooValue win, MooValue x, MooValue y, MooValue z) {
    (void)win;
    glTranslatef((float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z));
}

void moo_3d_push(MooValue win) {
    (void)win;
    glPushMatrix();
}

void moo_3d_pop(MooValue win) {
    (void)win;
    glPopMatrix();
}

// ============================================================
// 3D Zeichnen
// ============================================================

void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win;
    Color3 c = parse_color3(color);
    glColor3f(c.r, c.g, c.b);

    // Normale berechnen fuer Lighting
    float ax = (float)MV_NUM(x2) - (float)MV_NUM(x1);
    float ay = (float)MV_NUM(y2) - (float)MV_NUM(y1);
    float az = (float)MV_NUM(z2) - (float)MV_NUM(z1);
    float bx = (float)MV_NUM(x3) - (float)MV_NUM(x1);
    float by = (float)MV_NUM(y3) - (float)MV_NUM(y1);
    float bz = (float)MV_NUM(z3) - (float)MV_NUM(z1);
    float nx = ay*bz - az*by;
    float ny = az*bx - ax*bz;
    float nz = ax*by - ay*bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 0) { nx/=len; ny/=len; nz/=len; }

    glBegin(GL_TRIANGLES);
    glNormal3f(nx, ny, nz);
    glVertex3f((float)MV_NUM(x1), (float)MV_NUM(y1), (float)MV_NUM(z1));
    glVertex3f((float)MV_NUM(x2), (float)MV_NUM(y2), (float)MV_NUM(z2));
    glVertex3f((float)MV_NUM(x3), (float)MV_NUM(y3), (float)MV_NUM(z3));
    glEnd();
}

void moo_3d_cube(MooValue win, MooValue x, MooValue y, MooValue z,
                 MooValue size, MooValue color) {
    (void)win;
    Color3 c = parse_color3(color);
    glColor3f(c.r, c.g, c.b);

    float cx = (float)MV_NUM(x);
    float cy = (float)MV_NUM(y);
    float cz = (float)MV_NUM(z);
    float s = (float)MV_NUM(size) / 2.0f;

    glBegin(GL_QUADS);
    // Vorne
    glNormal3f(0, 0, 1);
    glVertex3f(cx-s, cy-s, cz+s); glVertex3f(cx+s, cy-s, cz+s);
    glVertex3f(cx+s, cy+s, cz+s); glVertex3f(cx-s, cy+s, cz+s);
    // Hinten
    glNormal3f(0, 0, -1);
    glVertex3f(cx+s, cy-s, cz-s); glVertex3f(cx-s, cy-s, cz-s);
    glVertex3f(cx-s, cy+s, cz-s); glVertex3f(cx+s, cy+s, cz-s);
    // Oben
    glNormal3f(0, 1, 0);
    glVertex3f(cx-s, cy+s, cz+s); glVertex3f(cx+s, cy+s, cz+s);
    glVertex3f(cx+s, cy+s, cz-s); glVertex3f(cx-s, cy+s, cz-s);
    // Unten
    glNormal3f(0, -1, 0);
    glVertex3f(cx-s, cy-s, cz-s); glVertex3f(cx+s, cy-s, cz-s);
    glVertex3f(cx+s, cy-s, cz+s); glVertex3f(cx-s, cy-s, cz+s);
    // Rechts
    glNormal3f(1, 0, 0);
    glVertex3f(cx+s, cy-s, cz+s); glVertex3f(cx+s, cy-s, cz-s);
    glVertex3f(cx+s, cy+s, cz-s); glVertex3f(cx+s, cy+s, cz+s);
    // Links
    glNormal3f(-1, 0, 0);
    glVertex3f(cx-s, cy-s, cz-s); glVertex3f(cx-s, cy-s, cz+s);
    glVertex3f(cx-s, cy+s, cz+s); glVertex3f(cx-s, cy+s, cz-s);
    glEnd();
}

void moo_3d_sphere(MooValue win, MooValue x, MooValue y, MooValue z,
                   MooValue radius, MooValue color, MooValue detail) {
    (void)win;
    Color3 c = parse_color3(color);
    glColor3f(c.r, c.g, c.b);

    float cx = (float)MV_NUM(x);
    float cy = (float)MV_NUM(y);
    float cz = (float)MV_NUM(z);
    float r = (float)MV_NUM(radius);
    int slices = (detail.tag == MOO_NUMBER) ? (int)MV_NUM(detail) : 16;
    if (slices < 4) slices = 4;
    if (slices > 64) slices = 64;
    int stacks = slices;

    // UV-Sphere mit Triangle-Strips
    for (int i = 0; i < stacks; i++) {
        float lat0 = (float)M_PI * (-0.5f + (float)i / stacks);
        float lat1 = (float)M_PI * (-0.5f + (float)(i + 1) / stacks);
        float y0 = sinf(lat0), yr0 = cosf(lat0);
        float y1 = sinf(lat1), yr1 = cosf(lat1);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; j++) {
            float lng = 2.0f * (float)M_PI * (float)j / slices;
            float xn = cosf(lng), zn = sinf(lng);

            glNormal3f(xn * yr1, y1, zn * yr1);
            glVertex3f(cx + r * xn * yr1, cy + r * y1, cz + r * zn * yr1);
            glNormal3f(xn * yr0, y0, zn * yr0);
            glVertex3f(cx + r * xn * yr0, cy + r * y0, cz + r * zn * yr0);
        }
        glEnd();
    }
}

// GLFW-basierte Tastaturabfrage fuer 3D-Fenster
MooValue moo_3d_key_pressed(MooValue win, MooValue key) {
    MooWindow3D* mw = get_win3d(win);
    if (!mw || key.tag != MOO_STRING) return moo_bool(false);
    const char* name = MV_STR(key)->chars;
    int glfw_key = 0;
    if (strcmp(name, "oben") == 0 || strcmp(name, "up") == 0) glfw_key = GLFW_KEY_UP;
    else if (strcmp(name, "unten") == 0 || strcmp(name, "down") == 0) glfw_key = GLFW_KEY_DOWN;
    else if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) glfw_key = GLFW_KEY_LEFT;
    else if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) glfw_key = GLFW_KEY_RIGHT;
    else if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) glfw_key = GLFW_KEY_SPACE;
    else if (strcmp(name, "escape") == 0) glfw_key = GLFW_KEY_ESCAPE;
    else return moo_bool(false);
    return moo_bool(glfwGetKey(mw->window, glfw_key) == GLFW_PRESS);
}
