#version 450
layout(location=0) in vec3 vColor;
layout(location=1) in vec3 vNormal;
layout(location=2) in float vDist;

layout(set=0, binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec3 fogColor;
} ubo;

layout(location=0) out vec4 fragColor;

void main() {
    // Step A: vColor + Fog (KEIN Lighting)
    float fog = clamp(vDist / ubo.fogDist, 0.0, 1.0);
    fragColor = vec4(mix(vColor, ubo.fogColor, fog * fog), 1.0);
}
