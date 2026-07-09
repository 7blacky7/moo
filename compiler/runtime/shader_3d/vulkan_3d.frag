#version 450
/* Vulkan-3D Fragment-Shader (Quelle fuer moo_3d_vulkan_frag_spv.h).
 * Regenerieren: skripte/3d_shader_build.sh (braucht glslc).
 * Formel bewusst identisch zum GL33-Shader (moo_3d_gl33_shaders.h),
 * damit beide Backends gleich aussehen. Ambient wird in fogColor.a
 * transportiert (vk_set_ambient), Transparenz in ubo.alpha. */
layout(location=0) in vec3 vColor;
layout(location=1) in vec3 vNormal;
layout(location=2) in float vDist;
layout(location=3) in float vWorldY;
layout(location=4) in vec3 vWorldPos;

layout(binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec4 fogColor;
    vec4 eyePos;
    vec4 lightColor;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float alpha;      /* per-Draw Transparenz (raum_transparenz) */
    float waveAmp;
    float waveFreq;
    float waveSpeed;
    float time;
    float specStrength;
    float specPower;
} pc;

layout(location=0) out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(ubo.lightDir);
    float diff = max(dot(N, L), 0.0);
    float ambient = clamp(ubo.fogColor.a, 0.0, 1.0);
    vec3 lit = vColor * ubo.lightColor.rgb * (ambient + (1.0 - ambient) * diff);
    if (pc.specStrength > 0.0) {
        vec3 V = normalize(ubo.eyePos.xyz - vWorldPos);
        vec3 H = normalize(L + V);
        lit += ubo.lightColor.rgb * pc.specStrength * pow(max(dot(N, H), 0.0), pc.specPower);
    }
    float density = 1.0 / max(ubo.fogDist, 1.0);
    float distFog = 1.0 - exp(-vDist * density);
    float heightFog = exp(-max(vWorldY, 0.0) * 0.08);
    float totalFog = clamp(distFog * heightFog, 0.0, 1.0);
    fragColor = vec4(mix(lit, ubo.fogColor.rgb, totalFog), pc.alpha);
}
