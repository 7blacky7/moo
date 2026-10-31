#ifndef MOO_3D_PBR_GLSL_H
#define MOO_3D_PBR_GLSL_H
/* Single-scatter GGX, exact separable Smith and conservative two-sided Fresnel diffuse. */
#define MOO_PBR_GLSL \
"const float MOO_PBR_PI=3.14159265358979323846;\n" \
"vec3 moo_pbr_fresnel(vec3 f0,float cosine) {\n" \
"    float x=1.0-clamp(cosine,0.0,1.0), x2=x*x;\n" \
"    return f0+(vec3(1)-f0)*(x2*x2*x);\n" \
"}\n" \
"vec3 moo_pbr_diffuse(vec3 base,float metal,float NoV,float NoL) {\n" \
"    vec3 f0=mix(vec3(.04),base,metal);\n" \
"    return (1.0-metal)*base/MOO_PBR_PI *\n" \
"           (vec3(1)-moo_pbr_fresnel(f0,NoV)) *\n" \
"           (vec3(1)-moo_pbr_fresnel(f0,NoL));\n" \
"}\n" \
"vec3 moo_pbr_specular(vec3 base,float metal,float roughness,float NoV,float NoL,float NoH,float VoH) {\n" \
"    if(NoV<=0.0 || NoL<=0.0 || VoH<=0.0) return vec3(0);\n" \
"    float r=clamp(roughness,.05,1.0), alpha=r*r, a2=alpha*alpha;\n" \
"    float nh=clamp(NoH,0.0,1.0);\n" \
"    float denom=(1.0-nh*nh)+a2*nh*nh;\n" \
"    float D=a2/(MOO_PBR_PI*denom*denom);\n" \
"    /* Exact separable Smith G/(4*NoV*NoL), without grazing divisions. */\n" \
"    float visibility=1.0/((NoV+sqrt(a2+(1.0-a2)*NoV*NoV)) *\n" \
"                         (NoL+sqrt(a2+(1.0-a2)*NoL*NoL)));\n" \
"    return D*visibility*moo_pbr_fresnel(mix(vec3(.04),base,metal),VoH);\n" \
"}\n" \
"vec3 moo_pbr_direct(vec3 base,float metal,float roughness,vec3 N,vec3 V,vec3 direction) {\n" \
"    float l2=dot(direction,direction);\n" \
"    if(l2<=1e-12) return vec3(0);\n" \
"    vec3 L=direction*inversesqrt(l2);\n" \
"    float NoV=clamp(dot(N,V),0.0,1.0), NoL=clamp(dot(N,L),0.0,1.0);\n" \
"    vec3 halfsum=V+L; float h2=dot(halfsum,halfsum);\n" \
"    if(NoV<=0.0 || NoL<=0.0 || h2<=1e-12) return vec3(0);\n" \
"    vec3 H=halfsum*inversesqrt(h2);\n" \
"    return (moo_pbr_diffuse(base,metal,NoV,NoL) +\n" \
"            moo_pbr_specular(base,metal,roughness,NoV,NoL,max(dot(N,H),0.0),max(dot(V,H),0.0)))*NoL;\n" \
"}\n"
#endif
