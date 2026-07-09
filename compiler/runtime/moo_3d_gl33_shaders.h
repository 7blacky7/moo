#ifndef MOO_3D_GL33_SHADERS_H
#define MOO_3D_GL33_SHADERS_H

/**
 * moo_3d_gl33_shaders.h — Eingebettete GLSL 330 Shader + Kompilierungs-Utilities.
 * Wird von moo_3d_gl33.c inkludiert.
 */

#include "glad/include/glad/glad.h"
#include <stdio.h>

// === Eingebettete Shader-Quellen ===

static const char* GL33_VERTEX_SHADER =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aColor;\n"
    "layout(location=2) in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "uniform float uWaveAmp;\n"
    "uniform float uWaveFreq;\n"
    "uniform float uWaveSpeed;\n"
    "uniform float uTime;\n"
    "out vec3 vColor;\n"
    "out vec3 vNormal;\n"
    "out float vDist;\n"
    "out float vWorldY;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    vec3 pos = aPos;\n"
    "    vec3 nrm = aNormal;\n"
    "    if (uWaveAmp > 0.0) {\n"
    "        float p1 = uWaveFreq * (pos.x * 0.9 + pos.z * 0.7) + uTime * uWaveSpeed;\n"
    "        float p2 = uWaveFreq * (pos.z * 1.3 - pos.x * 0.4) + uTime * uWaveSpeed * 0.77;\n"
    "        pos.y += uWaveAmp * (sin(p1) + 0.6 * sin(p2));\n"
    "        float dx = uWaveAmp * uWaveFreq * (cos(p1) * 0.9 - 0.24 * cos(p2));\n"
    "        float dz = uWaveAmp * uWaveFreq * (cos(p1) * 0.7 + 0.78 * cos(p2));\n"
    "        nrm = normalize(vec3(-dx, 1.0, -dz));\n"
    "    }\n"
    "    vec4 clipPos = uMVP * vec4(pos, 1.0);\n"
    "    gl_Position = clipPos;\n"
    "    vColor = aColor;\n"
    "    vNormal = mat3(uModel) * nrm;\n"
    "    vec4 worldPos = uModel * vec4(pos, 1.0);\n"
    "    vWorldY = worldPos.y;\n"
    "    vWorldPos = worldPos.xyz;\n"
    "    vDist = length(clipPos.xyz / clipPos.w);\n"
    "}\n";

static const char* GL33_FRAGMENT_SHADER =
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "in vec3 vNormal;\n"
    "in float vDist;\n"
    "in float vWorldY;\n"
    "in vec3 vWorldPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uEyePos;\n"
    "uniform vec3 uLightColor;\n"
    "uniform float uSpecStrength;\n"
    "uniform float uSpecPower;\n"
    "uniform vec3 uFogColor;\n"
    "uniform float uFogDist;\n"
    "uniform float uAmbient;\n"
    "uniform float uAlpha;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    vec3 lit = vColor * uLightColor * (uAmbient + (1.0 - uAmbient) * diff);\n"
    "    if (uSpecStrength > 0.0) {\n"
    "        vec3 V = normalize(uEyePos - vWorldPos);\n"
    "        vec3 H = normalize(L + V);\n"
    "        lit += uLightColor * uSpecStrength * pow(max(dot(N, H), 0.0), uSpecPower);\n"
    "    }\n"
    "    float density = 1.0 / max(uFogDist, 1.0);\n"
    "    float distFog = 1.0 - exp(-vDist * density);\n"
    "    float heightFog = exp(-max(vWorldY, 0.0) * 0.08);\n"
    "    float totalFog = clamp(distFog * heightFog, 0.0, 1.0);\n"
    "    fragColor = vec4(mix(lit, uFogColor, totalFog), uAlpha);\n"
    "}\n";

// === Shader-Kompilierung ===

static GLuint gl33_compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "moo GL33 Shader-Fehler: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint gl33_create_program(void) {
    GLuint vert = gl33_compile_shader(GL_VERTEX_SHADER, GL33_VERTEX_SHADER);
    if (!vert) return 0;

    GLuint frag = gl33_compile_shader(GL_FRAGMENT_SHADER, GL33_FRAGMENT_SHADER);
    if (!frag) {
        glDeleteShader(vert);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "moo GL33 Link-Fehler: %s\n", log);
        glDeleteProgram(program);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return 0;
    }

    // Shader-Objekte nach dem Linken freigeben
    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

// === Uniform-Locations (gecacht im Context) ===

typedef struct {
    GLint mvp;
    GLint model;
    GLint light_dir;
    GLint fog_color;
    GLint fog_dist;
    GLint ambient;
    GLint alpha;
    GLint wave_amp;
    GLint wave_freq;
    GLint wave_speed;
    GLint time;
    GLint eye_pos;
    GLint light_color;
    GLint spec_strength;
    GLint spec_power;
} GL33Uniforms;

static GL33Uniforms gl33_get_uniforms(GLuint program) {
    GL33Uniforms u;
    u.mvp       = glGetUniformLocation(program, "uMVP");
    u.model     = glGetUniformLocation(program, "uModel");
    u.light_dir = glGetUniformLocation(program, "uLightDir");
    u.fog_color = glGetUniformLocation(program, "uFogColor");
    u.fog_dist  = glGetUniformLocation(program, "uFogDist");
    u.ambient   = glGetUniformLocation(program, "uAmbient");
    u.alpha     = glGetUniformLocation(program, "uAlpha");
    u.wave_amp  = glGetUniformLocation(program, "uWaveAmp");
    u.wave_freq = glGetUniformLocation(program, "uWaveFreq");
    u.wave_speed = glGetUniformLocation(program, "uWaveSpeed");
    u.time      = glGetUniformLocation(program, "uTime");
    u.eye_pos   = glGetUniformLocation(program, "uEyePos");
    u.light_color = glGetUniformLocation(program, "uLightColor");
    u.spec_strength = glGetUniformLocation(program, "uSpecStrength");
    u.spec_power    = glGetUniformLocation(program, "uSpecPower");
    return u;
}

static void gl33_upload_matrix(GLint location, const float* mat4) {
    glUniformMatrix4fv(location, 1, GL_FALSE, mat4);
}

static void gl33_upload_vec3(GLint location, float x, float y, float z) {
    glUniform3f(location, x, y, z);
}

static void gl33_upload_float(GLint location, float v) {
    glUniform1f(location, v);
}

#endif
