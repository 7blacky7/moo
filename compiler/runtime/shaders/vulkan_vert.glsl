#version 450
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

layout(set=0, binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec3 fogColor;
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
    vec4 worldPos = ubo.model * vec4(aPos, 1.0);
    vWorldY = worldPos.y;
    // Fog-Distanz im Clip-Space (Kamera bei Origin nach MVP)
    vDist = length(clipPos.xyz / clipPos.w);
}
