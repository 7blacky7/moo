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
    // DEBUG: Hardcode gruen um Shader-Pipeline zu testen
    fragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
