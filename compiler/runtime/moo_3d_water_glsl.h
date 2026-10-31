#ifndef MOO_3D_WATER_GLSL_H
#define MOO_3D_WATER_GLSL_H
/* Linear HDR straight-ray water. Independent opaque snapshot; no framebuffer feedback.
 * Sky gradient and tint are documented environment/inscatter approximations.
 */
#define MOO_WATER_GLSL \
"uniform float uWaterOn;\n" \
"uniform sampler2D uWaterColor;\n" \
"uniform sampler2D uWaterDepth;\n" \
"uniform vec2 uWaterSize;\n" \
"uniform vec2 uWaterProjection;\n" \
"uniform vec3 uWaterAbsorption;\n" \
"uniform vec3 uWaterTint;\n" \
"uniform vec3 uWaterSkyHorizon;\n" \
"uniform vec3 uWaterSkyZenith;\n" \
"uniform float uWaterIor;\n" \
"uniform float uWaterMaxDistance;\n" \
"float moo_water_view_depth(float depth) {\n" \
"    return uWaterProjection.y / (2.0 * depth - 1.0 + uWaterProjection.x);\n" \
"}\n" \
"vec3 moo_water_radiance() {\n" \
"    vec3 eyeDelta = uEyePos - vWorldPos;\n" \
"    float eyeDistance = length(eyeDelta);\n" \
"    vec3 V = eyeDelta / max(eyeDistance, 1e-6);\n" \
"    vec3 N = normalize(vNormal);\n" \
"    bool below = dot(N, V) < 0.0;\n" \
"    if (below) N = -N;\n" \
"    float cosI = clamp(dot(N, V), 0.0, 1.0);\n" \
"    float f0 = (uWaterIor - 1.0) / (uWaterIor + 1.0);\n" \
"    f0 *= f0;\n" \
"    float R = f0 + (1.0 - f0) * pow(1.0 - cosI, 5.0);\n" \
"    if (below) {\n" \
"        float sinT2 = uWaterIor * uWaterIor * (1.0 - cosI * cosI);\n" \
"        if (sinT2 >= 1.0) R = 1.0;\n" \
"        else {\n" \
"            float cosT = sqrt(max(0.0, 1.0 - sinT2));\n" \
"            float rs = (uWaterIor * cosI - cosT) / max(uWaterIor * cosI + cosT, 1e-6);\n" \
"            float rp = (cosI - uWaterIor * cosT) / max(cosI + uWaterIor * cosT, 1e-6);\n" \
"            R = 0.5 * (rs * rs + rp * rp);\n" \
"        }\n" \
"    }\n" \
"    vec2 uv = gl_FragCoord.xy / uWaterSize;\n" \
"    float opaqueDepth = texture(uWaterDepth, uv).r;\n" \
"    float surfaceZ = moo_water_view_depth(gl_FragCoord.z);\n" \
"    float path = uWaterMaxDistance;\n" \
"    if (opaqueDepth < 0.999999 && surfaceZ > 1e-6) {\n" \
"        float hitZ = moo_water_view_depth(opaqueDepth);\n" \
"        path = clamp((hitZ - surfaceZ) * eyeDistance / surfaceZ, 0.0, uWaterMaxDistance);\n" \
"    }\n" \
"    // Below surface: bounded straight eye-to-interface path, no refracted-ray claim.\n" \
"    if (below) path = min(eyeDistance, uWaterMaxDistance);\n" \
"    vec3 T = exp(-uWaterAbsorption * path);\n" \
"    vec3 behind = texture(uWaterColor, uv).rgb;\n" \
"    vec3 transmitted = behind * T + uWaterTint * (vec3(1.0) - T);\n" \
"    vec3 reflectedRay = reflect(-V, N);\n" \
"    vec3 reflected = mix(uWaterSkyHorizon, uWaterSkyZenith, clamp(reflectedRay.y, 0.0, 1.0));\n" \
"    // No local reflection data below water: medium tint is the explicit fallback.\n" \
"    if (below) reflected = uWaterTint;\n" \
"    return max(mix(transmitted, reflected, R), vec3(0.0));\n" \
"}\n"
#endif
