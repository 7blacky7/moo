#version 450
/* Vulkan-3D Vertex-Shader (Quelle fuer moo_3d_vulkan_vert_spv.h).
 * Regenerieren: skripte/3d_shader_build.sh (braucht glslc).
 * Layout MUSS zu VulkanUBO in moo_3d_vulkan.c passen (std140). */
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float alpha;   /* per-Draw Transparenz — gelesen im Fragment-Shader */
} pc;

layout(binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec4 fogColor;   /* rgb = Nebelfarbe, a = Ambient-Level */
} ubo;

layout(location=0) out vec3 vColor;
layout(location=1) out vec3 vNormal;
layout(location=2) out float vDist;
layout(location=3) out float vWorldY;

void main() {
    vec4 clipPos = pc.mvp * vec4(aPos, 1.0);
    gl_Position = clipPos;
    vColor = aColor;
    vNormal = mat3(ubo.model) * aNormal;
    vWorldY = (ubo.model * vec4(aPos, 1.0)).y;
    vDist = length(clipPos.xyz / clipPos.w);
}
