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
    "out vec3 vColor;\n"
    "out vec3 vNormal;\n"
    "out float vDist;\n"
    "out float vWorldY;\n"
    "void main() {\n"
    "    vec4 clipPos = uMVP * vec4(aPos, 1.0);\n"
    "    gl_Position = clipPos;\n"
    "    vColor = aColor;\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    vec4 worldPos = uModel * vec4(aPos, 1.0);\n"
    "    vWorldY = worldPos.y;\n"
    "    vDist = length(clipPos.xyz / clipPos.w);\n"
    "}\n";

static const char* GL33_FRAGMENT_SHADER =
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "in vec3 vNormal;\n"
    "in float vDist;\n"
    "in float vWorldY;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uFogColor;\n"
    "uniform float uFogDist;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);\n"
    "    vec3 lit = vColor * (0.15 + 0.85 * diff);\n"
    "    float density = 1.0 / max(uFogDist, 1.0);\n"
    "    float distFog = 1.0 - exp(-vDist * density);\n"
    "    float heightFog = exp(-max(vWorldY, 0.0) * 0.08);\n"
    "    float totalFog = clamp(distFog * heightFog, 0.0, 1.0);\n"
    "    fragColor = vec4(mix(lit, uFogColor, totalFog), 1.0);\n"
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
} GL33Uniforms;

static GL33Uniforms gl33_get_uniforms(GLuint program) {
    GL33Uniforms u;
    u.mvp       = glGetUniformLocation(program, "uMVP");
    u.model     = glGetUniformLocation(program, "uModel");
    u.light_dir = glGetUniformLocation(program, "uLightDir");
    u.fog_color = glGetUniformLocation(program, "uFogColor");
    u.fog_dist  = glGetUniformLocation(program, "uFogDist");
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
