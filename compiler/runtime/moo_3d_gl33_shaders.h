#ifndef MOO_3D_GL33_SHADERS_H
#define MOO_3D_GL33_SHADERS_H

/**
 * moo_3d_gl33_shaders.h — Eingebettete GLSL 330 Shader + Kompilierungs-Utilities.
 * Wird von moo_3d_gl33.c inkludiert.
 */

#include "glad/include/glad/glad.h"
#include <stdio.h>
#include "moo_3d_water_glsl.h"
#include "moo_3d_fog_glsl.h"
#include "moo_3d_pbr_glsl.h"

// === Eingebettete Shader-Quellen ===

static const char* GL33_VERTEX_SHADER =
    "#version 330 core\n"
    "uniform int uHDR;\n"
    "vec3 hdrDecode(vec3 c) { return uHDR==0 ? c : mix(pow(max((c+.055)/1.055,vec3(0)),vec3(2.4)),c/12.92,lessThanEqual(c,vec3(.04045))); }\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aColor;\n"
    "layout(location=2) in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "uniform mat3 uNormalMat;\n"
    "uniform float uWaveAmp;\n"
    "uniform float uWaveFreq;\n"
    "uniform float uWaveSpeed;\n"
    "uniform float uTime;\n"
    "uniform vec3 uEyePos;\n"
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
    "    vColor = hdrDecode(aColor);\n"
    /* Normal-Matrix = transpose(inverse(M)): korrekt auch bei raum_skaliere. */
    "    vNormal = uNormalMat * nrm;\n"
    "    vec4 worldPos = uModel * vec4(pos, 1.0);\n"
    "    vWorldY = worldPos.y;\n"
    "    vWorldPos = worldPos.xyz;\n"
    /* Nebel-Distanz in Weltkoordinaten (Kamera -> Punkt); NDC-Laenge war praktisch konstant. */
    "    vDist = length(uEyePos - worldPos.xyz);\n"
    "}\n";

static const char* GL33_FRAGMENT_SHADER =
    "#version 330 core\n"
    "uniform int uHDR;\n"
    "vec3 hdrDecode(vec3 c) { return uHDR==0 ? c : mix(pow(max((c+.055)/1.055,vec3(0)),vec3(2.4)),c/12.92,lessThanEqual(c,vec3(.04045))); }\n"
    "in vec3 vColor;\n"
    "in vec3 vNormal;\n"
    "in float vDist;\n"
    "in float vWorldY;\n"
    "in vec3 vWorldPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uEyePos;\n"
    "uniform vec3 uLightColor;\n"
    "uniform float uLightLinear[8];\n"
    "uniform vec3 uPBR;\n"
    "vec3 lightDecode(vec3 c,int slot) { return uHDR!=0 && uLightLinear[slot]>.5 ? c : hdrDecode(c); }\n"
    "uniform float uSpecStrength;\n"
    "uniform float uSpecPower;\n"
    "uniform vec3 uFogColor;\n"
    "uniform float uFogDist;\n"
    "uniform float uAmbient;\n"
    "uniform float uAlpha;\n"
    "uniform vec3 uTint;\n"
    /* ERDE-7: Richtungslichter 1..3 (Licht 0 = uLightDir/uLightColor) + Punktlichter. */
    "uniform vec3 uExtraDir[3];\n"
    "uniform vec3 uExtraCol[3];\n"
    "uniform vec3 uPointPos[4];\n"
    "uniform vec3 uPointCol[4];\n"
    "uniform float uPointRad[4];\n"
    /* ERDE-8: Eigenleuchten; uUnlit = Himmel (keine Beleuchtung, kein Nebel). */
    "uniform float uEmissive;\n"
    "uniform float uUnlit;\n"
    /* ERDE-15: triplanare Textur in Weltkoordinaten (Unit 1). */
    "uniform sampler2D uTex;\n"
    "uniform float uTexOn;\n"
    "uniform float uTexScale;\n"
    /* Licht-Stufe 4: Shadow-Map des Hauptlichts (Unit 2, Hardware-Vergleich). */
    "uniform sampler2DShadow uShadowMap;\n"
    "uniform mat4 uLightSpace;\n"
    "uniform float uShadowOn;\n"
    MOO_PBR_GLSL
    MOO_WATER_GLSL
    MOO_FOG_GLSL
    "layout(location=0) out vec4 fragColor;\n"
    "layout(location=1) out vec4 fragAmbient;\n"
    "void main() {\n"
    "    fragAmbient=vec4(0,0,0,uAlpha);\n"
    "    if (uUnlit > 0.5) { fragColor = vec4(vColor * hdrDecode(uTint), uAlpha); return; }\n"
    "    if (uWaterOn > 0.5) { fragColor = vec4(moo_water_radiance(), 1.0); return; }\n"
    "    float sourceNormal2=dot(vNormal,vNormal);\n"
    "    vec3 N = uPBR.x>.5 && sourceNormal2<=1e-12 ? vec3(0,0,1) : normalize(vNormal);\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float sh = 1.0;\n"
    "    if (uShadowOn > 0.5) {\n"
    "        vec4 lp = uLightSpace * vec4(vWorldPos, 1.0);\n"
    "        vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;\n"
    "        if (sc.z < 1.0 && sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0) {\n"
    "            float bias = max(0.002 * (1.0 - dot(N, L)), 0.0006);\n"
    "            vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));\n"
    "            float acc = 0.0;\n"
    "            for (int sx = -1; sx <= 1; sx++)\n"
    "                for (int sy = -1; sy <= 1; sy++)\n"
    "                    acc += texture(uShadowMap, vec3(sc.xy + vec2(sx, sy) * texel, sc.z - bias));\n"
    "            sh = acc / 9.0;\n"
    "        }\n"
    "    }\n"
    "    vec3 base = vColor * hdrDecode(uTint);\n"
    "    if (uTexOn > 0.5) {\n"
    "        vec3 bw = abs(N);\n"
    "        bw /= max(bw.x + bw.y + bw.z, 1e-5);\n"
    "        vec3 tp = vWorldPos * uTexScale;\n"
    "        base *= texture(uTex, tp.zy).rgb * bw.x + texture(uTex, tp.xz).rgb * bw.y + texture(uTex, tp.xy).rgb * bw.z;\n"
    "    }\n"
    "    vec3 lit;\n"
    "    vec3 ambientPart=base*lightDecode(uLightColor,0)*uAmbient;\n"
    "    if(uPBR.x>.5) {\n"
    "    float metal=clamp(uPBR.y,0.0,1.0);\n"
    "    float ambient=clamp(uAmbient,0.0,1.0);\n"
    "    vec3 pbase=clamp(base,vec3(0),vec3(1));\n"
    "    lit=(1.0-metal)*pbase*ambient;\n"
    "    ambientPart=lit;\n"
    "    vec3 deltaV=uEyePos-vWorldPos;\n"
    "    float v2=dot(deltaV,deltaV), n2=dot(vNormal,vNormal);\n"
    "    if(v2>1e-12 && n2>1e-12) {\n"
    "        vec3 V=deltaV*inversesqrt(v2), pn=vNormal*inversesqrt(n2);\n"
    "        /* Explicit legacy light adapter Li=pi*linear light coefficient. */\n"
    "        lit+=moo_pbr_direct(pbase,metal,uPBR.z,pn,V,uLightDir) *\n"
    "             (MOO_PBR_PI*lightDecode(uLightColor,0))*(1.0-ambient)*sh;\n"
    "        for(int i=0;i<3;i++)\n"
    "            lit+=moo_pbr_direct(pbase,metal,uPBR.z,pn,V,uExtraDir[i]) *\n"
    "                 (MOO_PBR_PI*lightDecode(uExtraCol[i],i+1))*(1.0-ambient);\n"
    "        for(int i=0;i<4;i++) if(uPointRad[i]>0.0) {\n"
    "            vec3 d=uPointPos[i]-vWorldPos;\n"
    "            float att=clamp(1.0-length(d)/uPointRad[i],0.0,1.0);\n"
    "            lit+=moo_pbr_direct(pbase,metal,uPBR.z,pn,V,d) *\n"
    "                 (MOO_PBR_PI*lightDecode(uPointCol[i],i+4))*att*att;\n"
    "        }\n"
    "    }\n"
    "    } else {\n"
    "    vec3 light = lightDecode(uLightColor,0) * (uAmbient + (1.0 - uAmbient) * diff * sh);\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        if (uExtraCol[i] != vec3(0.0))\n"
    "            light += lightDecode(uExtraCol[i],i+1) * (1.0 - uAmbient) * max(dot(N, normalize(uExtraDir[i])), 0.0);\n"
    "    }\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        if (uPointRad[i] > 0.0) {\n"
    "            vec3 d = uPointPos[i] - vWorldPos;\n"
    "            float dist = length(d);\n"
    "            float att = clamp(1.0 - dist / uPointRad[i], 0.0, 1.0);\n"
    "            light += lightDecode(uPointCol[i],i+4) * att * att * max(dot(N, d / max(dist, 1e-4)), 0.0);\n"
    "        }\n"
    "    }\n"
    "    lit = base * light;\n"
    "    if (uSpecStrength > 0.0) {\n"
    "        vec3 V = normalize(uEyePos - vWorldPos);\n"
    "        vec3 H = normalize(L + V);\n"
    "        lit += lightDecode(uLightColor,0) * uSpecStrength * pow(max(dot(N, H), 0.0), uSpecPower) * sh;\n"
    "    }\n"
    "    }\n"
    "    lit += base * uEmissive;\n"
    "    if (uFogAdditive != 0) ambientPart=vec3(0);\n"
    "    if (uHeightFogOn != 0) {\n"
    "        float fogT; fragColor = vec4(moo_height_fog(lit,fogT), uAlpha);\n"
    "        fragAmbient=vec4(ambientPart*fogT,uAlpha); return;\n"
    "    }\n"
    "    float density = uHDR != 0 ? uFogProfile.x : (uFogDist > 0.0 ? 1.0 / max(uFogDist, 1.0) : 0.0);\n"
    /* Pro Pixel aus vWorldPos: interpoliertes vDist ist bei grossen Dreiecken ungenau. */
    "    float distFog = 1.0 - exp(-length(uEyePos - vWorldPos) * density);\n"
    "    float heightFog = exp(-max(vWorldY, 0.0) * 0.08);\n"
    "    float totalFog = clamp(distFog * heightFog, 0.0, 1.0);\n"
    "    fragAmbient=vec4(ambientPart*(1.0-totalFog),uAlpha);\n"
    "    fragColor = vec4(mix(lit, hdrDecode(uFogColor), totalFog), uAlpha);\n"
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
    GLint light_linear;
    GLint pbr;
    GLint spec_strength;
    GLint spec_power;
    GLint normal_mat;
    GLint tint;
    GLint extra_dir;
    GLint extra_col;
    GLint point_pos;
    GLint point_col;
    GLint point_rad;
    GLint emissive;
    GLint unlit;
    GLint tex;
    GLint tex_on;
    GLint tex_scale;
    GLint shadow_map;
    GLint light_space;
    GLint shadow_on;
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
    u.light_linear = glGetUniformLocation(program, "uLightLinear");
    u.pbr = glGetUniformLocation(program, "uPBR");
    u.spec_strength = glGetUniformLocation(program, "uSpecStrength");
    u.spec_power    = glGetUniformLocation(program, "uSpecPower");
    u.normal_mat    = glGetUniformLocation(program, "uNormalMat");
    u.tint          = glGetUniformLocation(program, "uTint");
    u.extra_dir     = glGetUniformLocation(program, "uExtraDir");
    u.extra_col     = glGetUniformLocation(program, "uExtraCol");
    u.point_pos     = glGetUniformLocation(program, "uPointPos");
    u.point_col     = glGetUniformLocation(program, "uPointCol");
    u.point_rad     = glGetUniformLocation(program, "uPointRad");
    u.emissive      = glGetUniformLocation(program, "uEmissive");
    u.unlit         = glGetUniformLocation(program, "uUnlit");
    u.tex           = glGetUniformLocation(program, "uTex");
    u.tex_on        = glGetUniformLocation(program, "uTexOn");
    u.tex_scale     = glGetUniformLocation(program, "uTexScale");
    u.shadow_map    = glGetUniformLocation(program, "uShadowMap");
    u.light_space   = glGetUniformLocation(program, "uLightSpace");
    u.shadow_on     = glGetUniformLocation(program, "uShadowOn");
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
