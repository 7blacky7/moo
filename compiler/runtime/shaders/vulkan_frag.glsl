#version 450
layout(location=0) in vec3 vColor;
layout(location=1) in vec3 vNormal;
layout(location=2) in float vDist;
layout(location=3) in float vWorldY;

layout(set=0, binding=0) uniform UBO {
    mat4 model;
    vec3 lightDir;
    float fogDist;
    vec3 fogColor;
} ubo;

layout(location=0) out vec4 fragColor;

void main() {
    // Phong Diffuse Lighting (Sonne von oben)
    float diff = max(dot(normalize(vNormal), normalize(ubo.lightDir)), 0.0);
    vec3 lit = vColor * (0.15 + 0.85 * diff);

    // Exponentieller Distance-Fog
    float density = 1.0 / max(ubo.fogDist, 1.0);
    float distFog = 1.0 - exp(-vDist * density);

    // Höhenabhängiger Fog (dichter am Boden)
    float heightFog = exp(-max(vWorldY, 0.0) * 0.08);

    // Kombinierter Fog
    float totalFog = clamp(distFog * heightFog, 0.0, 1.0);

    fragColor = vec4(mix(lit, ubo.fogColor, totalFog), 1.0);
}
