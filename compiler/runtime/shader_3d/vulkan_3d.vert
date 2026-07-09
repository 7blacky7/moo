#version 450
/* Vulkan-3D Vertex-Shader (Quelle fuer moo_3d_vulkan_vert_spv.h).
 * Regenerieren: skripte/3d_shader_build.sh (braucht glslc).
 * Layout MUSS zu VulkanUBO in moo_3d_vulkan.c passen (std140). */
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float alpha;      /* per-Draw Transparenz — gelesen im Fragment-Shader */
    float waveAmp;    /* Wellen (raum_wellen): Amplitude, 0 = aus */
    float waveFreq;
    float waveSpeed;
    float time;
    float specStrength;
    float specPower;
} pc;

layout(binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec4 fogColor;   /* rgb = Nebelfarbe, a = Ambient-Level */
    vec4 eyePos;     /* Kamera-Position (Specular) */
    vec4 lightColor; /* Lichtfarbe (raum_lichtfarbe/raum_tageszeit) */
} ubo;

layout(location=0) out vec3 vColor;
layout(location=1) out vec3 vNormal;
layout(location=2) out float vDist;
layout(location=3) out float vWorldY;
layout(location=4) out vec3 vWorldPos;

void main() {
    vec3 pos = aPos;
    vec3 nrm = aNormal;
    if (pc.waveAmp > 0.0) {
        float p1 = pc.waveFreq * (pos.x * 0.9 + pos.z * 0.7) + pc.time * pc.waveSpeed;
        float p2 = pc.waveFreq * (pos.z * 1.3 - pos.x * 0.4) + pc.time * pc.waveSpeed * 0.77;
        pos.y += pc.waveAmp * (sin(p1) + 0.6 * sin(p2));
        float dx = pc.waveAmp * pc.waveFreq * (cos(p1) * 0.9 - 0.24 * cos(p2));
        float dz = pc.waveAmp * pc.waveFreq * (cos(p1) * 0.7 + 0.78 * cos(p2));
        nrm = normalize(vec3(-dx, 1.0, -dz));
    }
    vec4 clipPos = pc.mvp * vec4(pos, 1.0);
    gl_Position = clipPos;
    vColor = aColor;
    vNormal = mat3(ubo.model) * nrm;
    vec4 wp = ubo.model * vec4(pos, 1.0);
    vWorldY = wp.y;
    vWorldPos = wp.xyz;
    vDist = length(clipPos.xyz / clipPos.w);
}
