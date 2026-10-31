/* Actual production GLSL against independent A2 four-million-sample RGB oracles.
 * Oracle thought 08e6eb06-fd21-45b7-b391-c48caa60a495, convergence <=1.67e-4.
 * GPU 65536 GGX-NDF + cosine samples; abs2e-3 allows measured sampling/float error.
 * Unit incident radiance: no pi light adapter, ambient, fog, bloom or tonemap.
 */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d_gl33.c"
static int gfx_errors;
MooValue moo_string_new(const char* s) { (void)s; return (MooValue){0}; }
void moo_throw(MooValue v) { (void)v; ++gfx_errors; }
static const char* gfx_furnace_fs =
    "#version 330 core\n"
    MOO_PBR_GLSL
    "uniform vec3 uTestBase;\n"
    "uniform vec3 uTestParam;\n"
    "uniform int uProbe;\n"
    "out vec4 result;\n"
    "float radical(uint bits) {\n"
    " bits=(bits<<16u)|(bits>>16u);\n"
    " bits=((bits&0x55555555u)<<1u)|((bits&0xAAAAAAAAu)>>1u);\n"
    " bits=((bits&0x33333333u)<<2u)|((bits&0xCCCCCCCCu)>>2u);\n"
    " bits=((bits&0x0F0F0F0Fu)<<4u)|((bits&0xF0F0F0F0u)>>4u);\n"
    " bits=((bits&0x00FF00FFu)<<8u)|((bits&0xFF00FF00u)>>8u);\n"
    " return float(bits)*2.3283064365386963e-10;\n"
    "}\n"
    "void main() {\n"
    " float metal=uTestParam.x,r=clamp(uTestParam.y,.05,1.0),nv=uTestParam.z;\n"
    " float alpha=r*r,a2=alpha*alpha;\n"
    " if(uProbe==1) {\n"
    "  float nh=nv,nl=2.0*nh*nh-1.0,vh=nh;\n"
    "  float d=(1.0-nh*nh)+a2*nh*nh;\n"
    "  float pdf=a2/(MOO_PBR_PI*d*d*4.0);\n"
    "  float fs=moo_pbr_specular(uTestBase,metal,r,1.0,nl,nh,vh).r;\n"
    "  float G=2.0*nl/(nl+sqrt(a2+(1.0-a2)*nl*nl));\n"
    "  result=vec4(fs,fs*nl/pdf,moo_pbr_fresnel(mix(vec3(.04),uTestBase,metal),vh).r*G,1); return;\n"
    " }\n"
    " vec3 V=vec3(sqrt(max(0.0,1.0-nv*nv)),0,nv),spec=vec3(0),diff=vec3(0),specBlock=vec3(0),diffBlock=vec3(0);\n"
    " float maxnh=0.0,above=0.0,maxDelta=0.0;\n"
    " const int samples=65536;\n"
    " for(int i=0;i<samples;i++) {\n"
    "  float u=(float(i)+.5)/float(samples),phi=2.0*MOO_PBR_PI*radical(uint(i));\n"
    "  float nh=sqrt((1.0-u)/(1.0+(a2-1.0)*u)),sinH=sqrt(max(0.0,1.0-nh*nh));\n"
    "  maxnh=max(maxnh,nh); if(nh>1.0) above+=1.0;\n"
    "  vec3 H=vec3(cos(phi)*sinH,sin(phi)*sinH,nh),L=reflect(-V,H);\n"
    "  float vh=dot(V,H),nl=L.z;\n"
    "  if(vh>0.0 && nl>0.0) {\n"
    "   float denom=(1.0-nh)*(1.0+nh)+a2*nh*nh;\n"
    "   float pdf=a2*nh/(MOO_PBR_PI*denom*denom*4.0*vh);\n"
    "   vec3 weight=moo_pbr_specular(uTestBase,metal,r,nv,nl,nh,vh)*nl/pdf;\n"
    "   float gv=2.0*nv/(nv+sqrt(a2+(1.0-a2)*nv*nv));\n"
    "   float gl=2.0*nl/(nl+sqrt(a2+(1.0-a2)*nl*nl));\n"
    "   float simple=moo_pbr_fresnel(mix(vec3(.04),uTestBase,metal),vh).r*gv*gl*vh/(nv*nh);\n"
    "   maxDelta=max(maxDelta,abs(weight.r-simple)); specBlock+=weight;\n"
    "  }\n"
    "  float cosine=sqrt(1.0-u);\n"
    "  diffBlock+=moo_pbr_diffuse(uTestBase,metal,nv,cosine)*MOO_PBR_PI;\n"
    "  if((i&255)==255) { spec+=specBlock; diff+=diffBlock; specBlock=vec3(0); diffBlock=vec3(0); }\n"
    " }\n"
    " result=uProbe==2 ? vec4(maxnh,above,maxDelta,1) : vec4((spec+diff)/float(samples),1);\n"
    "}\n";
typedef struct { float roughness,nov,metallic; double expected[3]; } GfxOracle;
static const GfxOracle gfx_oracles[]={
    {0.05,1,0.5,{0.548149775,0.268413234,0.066714607}},
    {0.05,1,1,{0.799995031,0.29999814,0.049999695}},
    {0.05,0.1,0.5,{0.814279107,0.69956759,0.616891682}},
    {0.05,0.1,1,{0.917650299,0.71258336,0.610049891}},
    {0.05,0.02,0.5,{0.944706764,0.916708309,0.89676847}},
    {0.05,0.02,1,{0.970608674,0.920326189,0.895184946}},
    {0.35,1,0.5,{0.540552585,0.265343892,0.065909188}},
    {0.35,1,1,{0.785515513,0.294574317,0.049103719}},
    {0.35,0.1,0.5,{0.565260068,0.399272627,0.290958756}},
    {0.35,0.1,1,{0.746570665,0.438951877,0.285142483}},
    {0.35,0.02,0.5,{0.566249049,0.409971652,0.325892342}},
    {0.35,0.02,1,{0.78713495,0.480294582,0.326874398}},
    {1,1,0.5,{0.257050061,0.150607165,0.035555359}},
    {1,1,1,{0.245488978,0.092079376,0.015374575}},
    {1,0.1,0.5,{0.294663943,0.146395395,0.046940971}},
    {1,0.1,1,{0.449041824,0.176860821,0.04077032}},
    {1,0.02,0.5,{0.275642888,0.126948569,0.046660799}},
    {1,0.02,1,{0.48500251,0.193328298,0.047491193}}
};
static void gfx_check(float got,double expected,double tolerance,const char* label) {
    if(!isfinite(got) || fabs(got-expected)>tolerance)
        fprintf(stderr,"%s got %.9g expected %.9g tolerance %.9g\n",label,got,expected,tolerance);
    assert(isfinite(got) && fabs(got-expected)<=tolerance);
}
static float gfx_draw_r(GL33Context* ctx) {
    gl33_clear(ctx,0,0,0);
    gl33_camera(ctx,0,0,2,0,0,0);
    gl33_triangle(ctx,-10,-10,0,10,-10,0,0,10,0,1,1,1);
    float p[4]={0}; glReadPixels(80,60,1,1,GL_RGBA,GL_FLOAT,p);
    return p[0];
}
static void gfx_draw_contract(GL33Context* ctx) {
    gl33_set_hdr(ctx,1);
    gl33_set_pbr(ctx,1,1,.2f); gl33_set_hdr(ctx,0);
    float pbr_uniform[3]; glGetUniformfv(ctx->program,ctx->uniforms.pbr,pbr_uniform);
    assert(ctx->pbr[0]==0 && pbr_uniform[0]==0);
    gl33_set_hdr(ctx,1);
    gl33_perspective(ctx,45,.1f,100);
    gl33_set_fog_density(ctx,0);
    gl33_set_ambient(ctx,0);
    gl33_set_light_dir(ctx,0,0,1);
    for(int j=1;j<4;j++) gl33_set_extra_light(ctx,j,0,0,1,0,0,0);
    for(int j=0;j<4;j++) gl33_set_point_light(ctx,j,0,0,2,4,0,0,0);
    gl33_set_pbr(ctx,1,1,.5f);
    for(int slot=0;slot<8;slot++) {
        for(int j=0;j<8;j++) gl33_set_linear_light(ctx,j,0,0,0);
        gl33_set_linear_light(ctx,slot,.1f,.1f,.1f);
        float low=gfx_draw_r(ctx);
        gl33_set_linear_light(ctx,slot,.2f,.2f,.2f);
        float high=gfx_draw_r(ctx);
        assert(low>0 && isfinite(high));
        gfx_check(high/low,2,.006,"all eight PBR lights scale specular radiance");
    }
    for(int j=0;j<8;j++) gl33_set_linear_light(ctx,j,0,0,0);
    /* Independent normal-incidence oracle: roughness1 dielectric has diffuse .96^2
       plus specular .04/4; point slot additionally has (1-distance/radius)^2=.25. */
    for(int kind=0;kind<2;kind++) {
        for(int j=0;j<8;j++) gl33_set_linear_light(ctx,j,0,0,0);
        gl33_set_linear_light(ctx,kind?4:0,.2f,.2f,.2f);
        gl33_set_ambient(ctx,0); gl33_set_pbr(ctx,0,0,1);
        float lambert=gfx_draw_r(ctx);
        double expected_lambert=.2*(kind?.25:1);
        gfx_check(lambert,expected_lambert,.00015,"absolute legacy directional/point Lambert");
        gl33_set_pbr(ctx,1,0,1);
        float dielectric=gfx_draw_r(ctx);
        gfx_check(dielectric,expected_lambert*(.96*.96+.01),.00015,"absolute GGX dielectric and point attenuation");
        gfx_check((dielectric-expected_lambert*.01)/(.96*.96),expected_lambert,.0002,"Fresnel-corrected diffuse equals legacy Lambert");
    }
    for(int j=0;j<8;j++) gl33_set_linear_light(ctx,j,0,0,0);
    gl33_set_pbr(ctx,1,1,.5f);
    gl33_set_ambient(ctx,.5f);
    gfx_check(gfx_draw_r(ctx),0,0,"metal has no diffuse ambient fallback");
    gl33_set_pbr(ctx,1,0,.5f);
    gfx_check(gfx_draw_r(ctx),.5,.001,"dielectric neutral ambient fallback");
    gl33_set_ambient(ctx,0);
    gl33_set_linear_light(ctx,0,.1f,.1f,.1f);
    gl33_set_pbr(ctx,0,0,1);
    float legacy=gfx_draw_r(ctx);
    gl33_set_pbr(ctx,1,1,.5f); assert(gfx_draw_r(ctx)>0);
    gl33_set_pbr(ctx,0,0,1);
    gfx_check(gfx_draw_r(ctx),legacy,0,"PBR off exactly restores legacy draw");
    gl33_set_pbr(ctx,1,1,0);
    float floor0=gfx_draw_r(ctx);
    gl33_set_pbr(ctx,1,1,.05f);
    gfx_check(gfx_draw_r(ctx),floor0,0,"roughness zero equals documented floor .05");
    gl33_set_linear_light(ctx,0,1,1,1);
    gfx_check(gfx_draw_r(ctx),40000,64,"sharp white metal linear1 fits RGBA16F");
    gl33_set_linear_light(ctx,0,2,2,2);
    float overflow=gfx_draw_r(ctx);
    printf("RGBA16F overflow target %.9g versus mathematical radiance 80000\n",overflow);
    assert(!isnan(overflow) && overflow>=65504 && (isinf(overflow) || overflow==65504));
    puts("PASS RGBA16F boundary is explicit: sharp white metal linear2 exceeds target");
    gl33_set_linear_light(ctx,0,0,0,0);
    gl33_set_light_dir(ctx,0,0,0);
    gfx_check(gfx_draw_r(ctx),0,0,"zero light direction remains finite black");
    assert(glGetError()==GL_NO_ERROR && gfx_errors==0);
    puts("GFX-PBR actual draw contract PASS");
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    GL33Context* ctx=(GL33Context*)gl33_create_window("GGX furnace",161,121);
    assert(ctx && ctx->program);
    GLuint program=gl33_post_program(gfx_furnace_fs); assert(program);
    GLuint texture=0,fbo=0;
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,1,1); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(program); glBindVertexArray(ctx->imm_vao);
    GLint base=glGetUniformLocation(program,"uTestBase"),param=glGetUniformLocation(program,"uTestParam");
    glUniform3f(base,.8f,.3f,.05f);
    GLint probe=glGetUniformLocation(program,"uProbe");
    glUniform1i(probe,1);
    const float nhs[]={1.0f,.99999994f,.999999f,.99999f,.9999f,.999f,.99f,.9f};
    for(size_t k=0;k<sizeof(nhs)/sizeof(nhs[0]);k++) {
        glUniform3f(param,1,.05f,nhs[k]); glDrawArrays(GL_TRIANGLES,0,3);
        float point[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,point);
        double nh=nhs[k],r=(float).05f,a2=r*r*r*r,nl=2*nh*nh-1;
        double d=(1-nh)*(1+nh)+a2*nh*nh;
        double F=.8+.2*pow(1-nh,5);
        double expected=a2/(3.14159265358979323846*d*d)*F/(2*(nl+sqrt(a2+(1-a2)*nl*nl)));
        printf("PROBE nh=%.9g specGPU=%.9g specCPU=%.9g weight=%.9g simplified=%.9g\n",nh,point[0],expected,point[1],point[2]);
    }
    glUniform1i(probe,2); glUniform3f(param,1,.05f,1); glDrawArrays(GL_TRIANGLES,0,3);
    float diagnostic[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,diagnostic);
    printf("SAMPLE maxNoH=%.9g aboveOne=%.9g maxWeightDelta=%.9g\n",diagnostic[0],diagnostic[1],diagnostic[2]);
    glUniform1i(probe,0);
    for(size_t i=0;i<sizeof(gfx_oracles)/sizeof(gfx_oracles[0]);i++) {
        const GfxOracle* o=&gfx_oracles[i];
        glUniform3f(param,o->metallic,o->roughness,o->nov);
        glDrawArrays(GL_TRIANGLES,0,3);
        float pixel[4]={0}; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,pixel);
        for(int c=0;c<3;c++) {
            gfx_check(pixel[c],o->expected[c],.002,"independent RGB furnace oracle");
            assert(pixel[c]>=0 && pixel[c]<=1.002f);
        }
        printf("PASS GPU RGB furnace r=%.2f NoV=%.2f metal=%.1f RGB=%.7f,%.7f,%.7f\n",
            o->roughness,o->nov,o->metallic,pixel[0],pixel[1],pixel[2]);
    }
    glUniform3f(base,1,1,1);
    const float rough[]={.05f,.1f,.35f,.7f,1};
    const float view[]={1,.5f,.1f,.02f};
    const double reference[5][4]={
        {.9177143195,.9203007869,.9653699434,.9784880751},
        {.9177100942,.9204211746,.9544019615,.8621805928},
        {.9169991980,.9183920819,.6384043647,.4051192266},
        {.9056691836,.8875736576,.4384038789,.1759658564},
        {.8900206849,.8690087596,.3947595409,.1259875858}
    };
    for(int r=0;r<5;r++) for(int v=0;v<4;v++) {
        glUniform3f(param,0,rough[r],view[v]); glDrawArrays(GL_TRIANGLES,0,3);
        float p[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,p);
        for(int c=0;c<3;c++) { gfx_check(p[c],reference[r][v],.002,"white dielectric furnace oracle"); assert(p[c]<=1.002f); }
    }
    puts("PASS 20 white dielectric furnace cases include grazing");
    assert(glGetError()==GL_NO_ERROR && gfx_errors==0);
    glBindVertexArray(0); glUseProgram(0); glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteProgram(program); glDeleteTextures(1,&texture); glDeleteFramebuffers(1,&fbo);
    gfx_draw_contract(ctx);
    gl33_close(ctx); glfwTerminate();
    puts("GFX-PBR FURNACE PASS");
    return 0;
}
