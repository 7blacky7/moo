#version 330 core
in vec3 vColor;
in vec3 vNormal;
in float vDist;

uniform vec3 uLightDir;
uniform vec3 uFogColor;
uniform float uFogDist;

out vec4 fragColor;

void main() {
    // Phong Diffuse Lighting
    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    vec3 lit = vColor * (0.3 + 0.7 * diff);

    // Distance Fog (quadratic falloff)
    float fog = clamp(vDist / uFogDist, 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, fog * fog), 1.0);
}
