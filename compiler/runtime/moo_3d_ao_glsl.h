#ifndef MOO_3D_AO_GLSL_H
#define MOO_3D_AO_GLSL_H
/* Deterministic screen-space ambient heuristic, not GI or a radiometric AO integral.
 * Half texel k represents full texel 2*k. Missing samples are unoccluded.
 * Applied full-resolution R16F is shared by composition and code-value debug.
 * No pass reads any of its own draw attachments. */
#define MOO_AO_DEPTH_GLSL \
"uniform sampler2D uDepth;\n" \
"uniform ivec2 uFullSize;\n" \
"uniform vec4 uProjection;\n" \
"uniform float uRadius;\n" \
"bool moo_ao_finite(vec3 p) { return !any(isnan(p)) && !any(isinf(p)); }\n" \
"bool moo_ao_position(ivec2 pixel, out vec3 p) {\n" \
"    p=vec3(0);\n" \
"    if(any(lessThan(pixel,ivec2(0))) || any(greaterThanEqual(pixel,uFullSize))) return false;\n" \
"    float d=texelFetch(uDepth,pixel,0).r;\n" \
"    if(isnan(d) || isinf(d) || d<0.0 || d>=1.0) return false;\n" \
"    float divisor=2.0*d-1.0+uProjection.z;\n" \
"    if(abs(divisor)<1e-20 || abs(uProjection.x)<1e-20 || abs(uProjection.y)<1e-20) return false;\n" \
"    float z=uProjection.w/divisor;\n" \
"    vec2 ndc=(vec2(pixel)+vec2(.5))/vec2(uFullSize)*2.0-1.0;\n" \
"    p=vec3(ndc*z/uProjection.xy,-z);\n" \
"    return z>0.0 && moo_ao_finite(p);\n" \
"}\n" \
"ivec2 moo_ao_full_pixel(ivec2 halfPixel) { return min(halfPixel*2,uFullSize-ivec2(1)); }\n" \
"float moo_ao_depth_weight(float z0,float z1) {\n" \
"    float difference=abs(z0-z1);\n" \
"    float scale=max(uRadius*.1,1e-5);\n" \
"    return difference>max(uRadius,1e-5) ? 0.0 : exp(-difference/scale);\n" \
"}\n"

static const char* GL33_AO_CALCULATE_FS =
"#version 330 core\n"
MOO_AO_DEPTH_GLSL
"uniform float uStrength;\n"
"uniform float uBias;\n"
"layout(location=0) out float fragAO;\n"
"bool moo_ao_normal(ivec2 q,vec3 p,out vec3 n) {\n"
"    vec3 l,r,b,t;\n"
"    bool vl=moo_ao_position(q+ivec2(-1,0),l);\n"
"    bool vr=moo_ao_position(q+ivec2(1,0),r);\n"
"    bool vb=moo_ao_position(q+ivec2(0,-1),b);\n"
"    bool vt=moo_ao_position(q+ivec2(0,1),t);\n"
"    n=vec3(0,0,1);\n"
"    if((!vl&&!vr)||(!vb&&!vt)) return false;\n"
"    vec3 dx=vr && (!vl || abs(r.z-p.z)<=abs(l.z-p.z)) ? r-p : p-l;\n"
"    vec3 dy=vt && (!vb || abs(t.z-p.z)<=abs(b.z-p.z)) ? t-p : p-b;\n"
"    vec3 crossValue=cross(dx,dy);\n"
"    float n2=dot(crossValue,crossValue);\n"
"    if(!moo_ao_finite(crossValue) || n2<=1e-20) return false;\n"
"    n=crossValue*inversesqrt(n2);\n"
"    if(dot(n,-p)<0.0) n=-n;\n"
"    return true;\n"
"}\n"
"float moo_ao_sample(vec3 p,vec3 n,vec2 samplePixel) {\n"
"    if(any(isnan(samplePixel)) || any(isinf(samplePixel)) ||\n"
"       any(lessThan(samplePixel,vec2(0))) ||\n"
"       any(greaterThanEqual(samplePixel,vec2(uFullSize)))) return 0.0;\n"
"    vec3 q;\n"
"    if(!moo_ao_position(ivec2(samplePixel),q)) return 0.0;\n"
"    vec3 delta=q-p;\n"
"    float distance=length(delta);\n"
"    if(isnan(distance)||isinf(distance)||distance<=1e-7||distance>=uRadius) return 0.0;\n"
"    float cosine=max((dot(n,delta)-uBias)/distance,0.0);\n"
"    return clamp(cosine,0.0,1.0)*(1.0-distance/uRadius);\n"
"}\n"
"void main() {\n"
"    fragAO=1.0;\n"
"    if(uRadius<=0.0 || uStrength<=0.0) return;\n"
"    ivec2 pixel=moo_ao_full_pixel(ivec2(gl_FragCoord.xy));\n"
"    vec3 p,n;\n"
"    if(!moo_ao_position(pixel,p) || !moo_ao_normal(pixel,p,n)) return;\n"
"    vec2 screenRadius=abs(uProjection.xy)*vec2(uFullSize)*(.5*uRadius/(-p.z));\n"
"    const vec2 directions[8]=vec2[8](vec2(1,0),vec2(.70710678,.70710678),\n"
"      vec2(0,1),vec2(-.70710678,.70710678),vec2(-1,0),\n"
"      vec2(-.70710678,-.70710678),vec2(0,-1),vec2(.70710678,-.70710678));\n"
"    float sum=0.0;\n"
"    for(int i=0;i<8;i++) {\n"
"        vec2 delta=directions[i]*screenRadius;\n"
"        float nearAO=moo_ao_sample(p,n,vec2(pixel)+vec2(.5)+delta*.35);\n"
"        float farAO=moo_ao_sample(p,n,vec2(pixel)+vec2(.5)+delta*.75);\n"
"        sum+=max(nearAO,farAO);\n"
"    }\n"
"    fragAO=clamp(1.0-uStrength*(sum/8.0),0.0,1.0);\n"
"}\n";

static const char* GL33_AO_FILTER_FS =
"#version 330 core\n"
MOO_AO_DEPTH_GLSL
"uniform sampler2D uAO;\n"
"uniform ivec2 uAxis;\n"
"layout(location=0) out float fragAO;\n"
"void main() {\n"
"    fragAO=1.0;\n"
"    ivec2 hp=ivec2(gl_FragCoord.xy), size=textureSize(uAO,0);\n"
"    vec3 p;\n"
"    if(!moo_ao_position(moo_ao_full_pixel(hp),p)) return;\n"
"    float total=0.0,weights=0.0;\n"
"    for(int i=-1;i<=1;i++) {\n"
"        ivec2 q=hp+uAxis*i;\n"
"        if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,size))) continue;\n"
"        vec3 samplePosition;\n"
"        if(!moo_ao_position(moo_ao_full_pixel(q),samplePosition)) continue;\n"
"        float weight=(i==0?2.0:1.0)*moo_ao_depth_weight(p.z,samplePosition.z);\n"
"        total+=texelFetch(uAO,q,0).r*weight;\n"
"        weights+=weight;\n"
"    }\n"
"    if(weights>1e-8) fragAO=clamp(total/weights,0.0,1.0);\n"
"}\n";

static const char* GL33_AO_UPSAMPLE_FS =
"#version 330 core\n"
MOO_AO_DEPTH_GLSL
"uniform sampler2D uAO;\n"
"layout(location=0) out float fragAO;\n"
"void main() {\n"
"    fragAO=1.0;\n"
"    ivec2 pixel=ivec2(gl_FragCoord.xy);\n"
"    vec3 p;\n"
"    if(!moo_ao_position(pixel,p)) return;\n"
"    // Half sample k represents full pixel 2*k, including odd last dimensions.\n"
"    vec2 halfPosition=vec2(pixel)*.5;\n"
"    ivec2 base=ivec2(floor(halfPosition)), size=textureSize(uAO,0);\n"
"    vec2 fraction=fract(halfPosition);\n"
"    float total=0.0,weights=0.0;\n"
"    for(int y=0;y<2;y++) for(int x=0;x<2;x++) {\n"
"        ivec2 q=clamp(base+ivec2(x,y),ivec2(0),size-ivec2(1));\n"
"        vec3 samplePosition;\n"
"        if(!moo_ao_position(moo_ao_full_pixel(q),samplePosition)) continue;\n"
"        float wx=x==0?1.0-fraction.x:fraction.x;\n"
"        float wy=y==0?1.0-fraction.y:fraction.y;\n"
"        float weight=wx*wy*moo_ao_depth_weight(p.z,samplePosition.z);\n"
"        total+=texelFetch(uAO,q,0).r*weight;\n"
"        weights+=weight;\n"
"    }\n"
"    if(weights>1e-8) fragAO=clamp(total/weights,0.0,1.0);\n"
"}\n";

static const char* GL33_AO_COMPOSITE_FS =
"#version 330 core\n"
"uniform sampler2D uAmbient;\n"
"uniform sampler2D uAppliedAO;\n"
"layout(location=0) out vec4 fragColor;\n"
"void main() {\n"
"    ivec2 p=ivec2(gl_FragCoord.xy);\n"
"    float ao=clamp(texelFetch(uAppliedAO,p,0).r,0.0,1.0);\n"
"    fragColor=vec4(texelFetch(uAmbient,p,0).rgb*(1.0-ao),0.0);\n"
"}\n";

static const char* GL33_AO_DEBUG_FS =
"#version 330 core\n"
"uniform sampler2D uAppliedAO;\n"
"layout(location=0) out vec4 fragColor;\n"
"void main() {\n"
"    float ao=clamp(texelFetch(uAppliedAO,ivec2(gl_FragCoord.xy),0).r,0.0,1.0);\n"
"    fragColor=vec4(vec3(ao),1.0);\n"
"}\n";

#endif
