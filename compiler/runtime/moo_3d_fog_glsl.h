#ifndef MOO_3D_FOG_GLSL_H
#define MOO_3D_FOG_GLSL_H
#include "moo_3d_fog_math.h"
/* Air-only pass. Sky/overlay and snapshot-composited water bypass this path.
 * Finite working domain: |world/eye y| <= 1e7, distance <= 1e8.
 * Beyond this domain, bound the medium coordinates rather than overflow.
 * Solar lobe is a bounded artistic single-scattering source, not a phase PDF. */
#define MOO_FOG_GLSL \
"uniform int uHeightFogOn;\n" \
"uniform int uFogAdditive;\n" \
"uniform vec4 uFogProfile;\n" \
MOO_FOG_TAU_GLSL \
"vec3 moo_height_fog(vec3 radiance, out float T) {\n" \
"  T = 1.0;\n" \
"  vec3 ray = vWorldPos - uEyePos;\n" \
"  float distance = min(length(ray), 1e8);\n" \
"  if (isnan(distance) || isinf(distance)) return radiance;\n" \
"  float tau = moo_fog_tau(uFogProfile.x, distance, clamp(uEyePos.y,-1e7,1e7), clamp(vWorldPos.y,-1e7,1e7), uFogProfile.y, uFogProfile.z);\n" \
"  T = exp(-tau);\n" \
"  if (uFogAdditive != 0) return radiance * T;\n" \
"  float sunLength = length(uLightDir);\n" \
"  float phase = 0.0;\n" \
"  if (distance > 1e-6 && sunLength > 1e-6) phase = pow(max(dot(ray / max(length(ray),1e-6),uLightDir / sunLength),0.0),8.0);\n" \
"  vec3 source = hdrDecode(uFogColor) + lightDecode(uLightColor,0) * (uFogProfile.w * phase);\n" \
"  return radiance * T + source * (1.0-T);\n" \
"}\n" \
"vec3 moo_height_fog(vec3 radiance) { float T; return moo_height_fog(radiance,T); }\n"
#endif
