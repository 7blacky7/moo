/* GL33 HDR metering: exact valid-texel log-mean reduction, before bloom. */
#ifndef MOO_3D_EXPOSURE_GL33_H
#define MOO_3D_EXPOSURE_GL33_H
static const char* GL33_EXPOSURE_FS =
    "#version 330 core\n"
    "uniform sampler2D uSrc; uniform int uFirst; out vec2 meter;\n"
    "void main(){ ivec2 size=textureSize(uSrc,0), p=ivec2(gl_FragCoord.xy)*2;\n"
    " vec2 total=vec2(0); for(int y=0;y<2;y++) for(int x=0;x<2;x++){\n"
    "  ivec2 q=p+ivec2(x,y); if(any(greaterThanEqual(q,size))) continue;\n"
    "  vec4 v=texelFetch(uSrc,q,0);\n"
    "  if(uFirst!=0 && (any(isnan(v.rgb)) || any(isinf(v.rgb)))) continue;\n"
    "  if(uFirst!=0) total+=vec2(log(clamp(dot(max(v.rgb,vec3(0)),vec3(.2126,.7152,.0722)),1e-6,65504.0)),1);\n"
    "  else total+=v.rg; } meter=total; }\n";

static void gl33_exposure_release(GL33Context* ctx) {
    for(int i=0;i<32;i++) {
        if(ctx->exposure.fbo[i]) glDeleteFramebuffers(1,&ctx->exposure.fbo[i]);
        if(ctx->exposure.tex[i]) glDeleteTextures(1,&ctx->exposure.tex[i]);
        ctx->exposure.fbo[i]=ctx->exposure.tex[i]=0;
    }
    if(ctx->exposure.program) glDeleteProgram(ctx->exposure.program);
    ctx->exposure.program=0;
    ctx->exposure.levels=ctx->exposure.w=ctx->exposure.h=ctx->exposure.initialized=0;
}
static int gl33_exposure_targets(GL33Context* ctx) {
    if(ctx->exposure.w==ctx->hdr_w && ctx->exposure.h==ctx->hdr_h && ctx->exposure.levels) return 1;
    gl33_exposure_release(ctx);
    ctx->exposure.program=gl33_post_program(GL33_EXPOSURE_FS);
    if(!ctx->exposure.program) return 0; /* SceneEnd reports only after restoring the final frame. */
    int w=ctx->hdr_w,h=ctx->hdr_h,n=0;
    do {
        w=(w+1)/2; h=(h+1)/2;
        glGenFramebuffers(1,&ctx->exposure.fbo[n]);
        glBindFramebuffer(GL_FRAMEBUFFER,ctx->exposure.fbo[n]);
        glGenTextures(1,&ctx->exposure.tex[n]);
        glBindTexture(GL_TEXTURE_2D,ctx->exposure.tex[n]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG32F,w,h,0,GL_RG,GL_FLOAT,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ctx->exposure.tex[n],0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
            gl33_exposure_release(ctx);
            return 0;
        }
        ++n;
    } while((w>1 || h>1) && n<32);
    ctx->exposure.levels=n; ctx->exposure.w=ctx->hdr_w; ctx->exposure.h=ctx->hdr_h;
    return 1;
}
static void gl33_set_auto_exposure(void* vctx,float minimum,float maximum,float key,float tau) {
    GL33Context* ctx=(GL33Context*)vctx;
    if(!ctx || !ctx->hdr_on) { gl33_hdr_error("Auto-Belichtung: HDR muss aktiv sein"); return; }
    ctx->exposure.on=1; ctx->exposure.minimum=minimum; ctx->exposure.maximum=maximum;
    ctx->exposure.key=key; ctx->exposure.tau=tau; ctx->exposure.initialized=0;
}
static float gl33_exposure_value(void* vctx) {
    GL33Context* ctx=(GL33Context*)vctx;
    return ctx ? ctx->hdr_exposure : 1.0f;
}
static int gl33_exposure_update(GL33Context* ctx) {
    if(!ctx->exposure.on) return 1;
    glActiveTexture(GL_TEXTURE0);
    if(!gl33_exposure_targets(ctx)) return 0;
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(ctx->exposure.program);
    glUniform1i(glGetUniformLocation(ctx->exposure.program,"uSrc"),0);
    glBindVertexArray(ctx->imm_vao);
    GLuint src=ctx->hdr_color;
    int w=ctx->hdr_w,h=ctx->hdr_h;
    for(int i=0;i<ctx->exposure.levels;i++) {
        w=(w+1)/2; h=(h+1)/2;
        glBindFramebuffer(GL_FRAMEBUFFER,ctx->exposure.fbo[i]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0,0,w,h); glBindTexture(GL_TEXTURE_2D,src);
        glUniform1i(glGetUniformLocation(ctx->exposure.program,"uFirst"),i==0);
        glDrawArrays(GL_TRIANGLES,0,3);
        src=ctx->exposure.tex[i];
    }
    float sum_count[2]={0,0};
    glReadPixels(0,0,1,1,GL_RG,GL_FLOAT,sum_count);
    glBindVertexArray(0); glBindTexture(GL_TEXTURE_2D,0);
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,ctx->hdr_w,ctx->hdr_h);
    if(!isfinite(sum_count[0]) || !isfinite(sum_count[1]) || sum_count[1]<=0)
        return 0;
    float mean=expf(sum_count[0]/sum_count[1]);
    float target=fminf(ctx->exposure.maximum,fmaxf(ctx->exposure.minimum,ctx->exposure.key/mean));
    double now=ctx->render_time_fixed ? ctx->render_time_seconds : glfwGetTime();
    double dt=now-ctx->exposure.last_time;
    if(!ctx->exposure.initialized || dt<0) ctx->hdr_exposure=target;
    else if(dt>0) {
        float weight=ctx->exposure.tau==0 ? 1.0f : (float)-expm1(-dt/(double)ctx->exposure.tau);
        ctx->hdr_exposure+=(target-ctx->hdr_exposure)*weight;
    }
    ctx->exposure.mean_luminance=mean;
    ctx->exposure.last_time=now; ctx->exposure.initialized=1;
    return 1;
}
static void gl33_set_linear_light(void* vctx,int slot,float r,float g,float b) {
    GL33Context* ctx=(GL33Context*)vctx;
    if(!ctx || !ctx->hdr_on) { gl33_hdr_error("Lineares Licht erfordert HDR"); return; }
    if(slot<0 || slot>7) { gl33_hdr_error("Lineares Licht: Slot 0..7 erforderlich"); return; }
    glfwMakeContextCurrent(ctx->window);
    float* color=slot==0 ? ctx->light_color : slot<4 ? ctx->extra_col[slot-1] : ctx->point_col[slot-4];
    color[0]=r; color[1]=g; color[2]=b; ctx->light_linear[slot]=1.0f;
    gl33_upload_lighting(ctx);
}
#endif
