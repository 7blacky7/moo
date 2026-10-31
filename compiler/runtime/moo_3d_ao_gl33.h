#ifndef MOO_3D_AO_GL33_H
#define MOO_3D_AO_GL33_H
#include "moo_3d_ao_glsl.h"
static GLuint gl33_post_program(const char*);
typedef struct {
    GLint draw, read, buffers[2], read_buffer, viewport[4], program, vao;
    GLint active, texture[3], depth_func, src_rgb, dst_rgb, src_a, dst_a, eq_rgb, eq_a;
    GLboolean depth, blend, srgb, scissor, cull, depth_mask, color[4];
} GL33AOSaved;
static void gl33_ao_save(GL33AOSaved* s) {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s->draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s->read);
    glGetIntegerv(GL_DRAW_BUFFER0,&s->buffers[0]);
    glGetIntegerv(GL_DRAW_BUFFER1,&s->buffers[1]);
    glGetIntegerv(GL_READ_BUFFER,&s->read_buffer);
    glGetIntegerv(GL_VIEWPORT,s->viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM,&s->program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&s->active);
    for(int i=0;i<3;i++) {
        glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&s->texture[i]);
    }
    glActiveTexture(s->active);
    s->depth=glIsEnabled(GL_DEPTH_TEST); s->blend=glIsEnabled(GL_BLEND);
    s->srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB); s->scissor=glIsEnabled(GL_SCISSOR_TEST);
    s->cull=glIsEnabled(GL_CULL_FACE);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&s->depth_mask); glGetBooleanv(GL_COLOR_WRITEMASK,s->color);
    glGetIntegerv(GL_DEPTH_FUNC,&s->depth_func);
    glGetIntegerv(GL_BLEND_SRC_RGB,&s->src_rgb); glGetIntegerv(GL_BLEND_DST_RGB,&s->dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&s->src_a); glGetIntegerv(GL_BLEND_DST_ALPHA,&s->dst_a);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&s->eq_rgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&s->eq_a);
}
static void gl33_ao_enable(GLenum cap,GLboolean enabled) {
    if(enabled) glEnable(cap); else glDisable(cap);
}
static void gl33_ao_restore(const GL33AOSaved* s) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s->draw);
    GLenum buffers[2]={(GLenum)s->buffers[0],(GLenum)s->buffers[1]};
    if(s->draw) glDrawBuffers(2,buffers); else glDrawBuffer(buffers[0]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s->read); glReadBuffer(s->read_buffer);
    glViewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    glUseProgram(s->program); glBindVertexArray(s->vao);
    for(int i=0;i<3;i++) { glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,s->texture[i]); }
    glActiveTexture(s->active);
    gl33_ao_enable(GL_DEPTH_TEST,s->depth); gl33_ao_enable(GL_BLEND,s->blend);
    gl33_ao_enable(GL_FRAMEBUFFER_SRGB,s->srgb); gl33_ao_enable(GL_SCISSOR_TEST,s->scissor);
    gl33_ao_enable(GL_CULL_FACE,s->cull);
    glDepthFunc(s->depth_func); glDepthMask(s->depth_mask);
    glColorMask(s->color[0],s->color[1],s->color[2],s->color[3]);
    glBlendFuncSeparate(s->src_rgb,s->dst_rgb,s->src_a,s->dst_a);
    glBlendEquationSeparate(s->eq_rgb,s->eq_a);
}
static void gl33_ao_delete_targets(GL33AOState* a) {
    glDeleteTextures(1,&a->ambient); glDeleteTextures(3,a->tex);
    glDeleteFramebuffers(3,a->fbo); glDeleteFramebuffers(1,&a->composite_fbo);
    a->ambient=a->composite_fbo=0;
    memset(a->tex,0,sizeof(a->tex)); memset(a->fbo,0,sizeof(a->fbo)); a->w=a->h=0;
}
static void gl33_ao_release(GL33Context* ctx) {
    gl33_ao_delete_targets(&ctx->ao);
    for(int i=0;i<5;i++) { if(ctx->ao.program[i]) glDeleteProgram(ctx->ao.program[i]); ctx->ao.program[i]=0; }
}
static void gl33_ao_reset_mode(GL33Context* ctx) {
    ctx->ao.on=ctx->ao.debug=ctx->ao.opaque_done=ctx->ao.view_locked=0;
}
static int gl33_set_ao(void* v,int on,float radius,float strength,float bias) {
    GL33Context* ctx=(GL33Context*)v;
    if(!ctx || !ctx->hdr_on || (ctx->hdr_fbo && !ctx->hdr_finalized)) return 0;
    if(on && (!isfinite(radius)||!isfinite(strength)||!isfinite(bias)||
       radius<0||radius>10000||strength<0||strength>1||bias<0||bias>radius)) return 0;
    ctx->ao.on=!!on;
    if(on) { ctx->ao.radius=radius; ctx->ao.strength=strength; ctx->ao.bias=bias; }
    else ctx->ao.debug=0;
    return 1;
}
static int gl33_set_ao_debug(void* v,int on) {
    GL33Context* ctx=(GL33Context*)v;
    if(!ctx || !ctx->hdr_on || (ctx->hdr_fbo&&!ctx->hdr_finalized) || (on&&!ctx->ao.on)) return 0;
    ctx->ao.debug=!!on; return 1;
}
static void gl33_ao_texture(GLuint* texture,GLint format,int w,int h,GLenum channels) {
    glGenTextures(1,texture); glBindTexture(GL_TEXTURE_2D,*texture);
    glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,channels,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
static int gl33_ao_targets(GL33Context* ctx) {
    GL33AOState* a=&ctx->ao;
    if(!a->on) return 1;
    GL33AOSaved saved; gl33_ao_save(&saved);
    int ok=0;
    GLuint programs[5]={0};
    const char* sources[5]={GL33_AO_CALCULATE_FS,GL33_AO_FILTER_FS,GL33_AO_UPSAMPLE_FS,
                           GL33_AO_COMPOSITE_FS,GL33_AO_DEBUG_FS};
    GL33AOState next={0};
    if(!a->program[0]) {
        for(int i=0;i<5;i++) { programs[i]=gl33_post_program(sources[i]); if(!programs[i]) goto finish; }
        memcpy(a->program,programs,sizeof(programs)); memset(programs,0,sizeof(programs));
    }
    if(a->w==ctx->hdr_w && a->h==ctx->hdr_h && a->ambient &&
       a->scene_fbo==ctx->hdr_fbo && a->scene_color==ctx->hdr_color) { ok=1; goto finish; }
    next.w=ctx->hdr_w; next.h=ctx->hdr_h;
    glActiveTexture(GL_TEXTURE0);
    gl33_ao_texture(&next.ambient,GL_RGBA16F,next.w,next.h,GL_RGBA);
    glGenFramebuffers(3,next.fbo); glGenFramebuffers(1,&next.composite_fbo);
    for(int i=0;i<3;i++) {
        gl33_ao_texture(&next.tex[i],GL_R16F,i==2?next.w:(next.w+1)/2,i==2?next.h:(next.h+1)/2,GL_RED);
        glBindFramebuffer(GL_FRAMEBUFFER,next.fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,next.tex[i],0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto finish;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,next.composite_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ctx->hdr_color,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto finish;
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    GLint previous_ambient=0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&previous_ambient);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,next.ambient,0);
    { GLenum buffers[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; glDrawBuffers(2,buffers); }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,(GLuint)previous_ambient,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); goto finish;
    }
    gl33_ao_delete_targets(a);
    a->ambient=next.ambient; a->composite_fbo=next.composite_fbo; a->w=next.w; a->h=next.h;
    memcpy(a->tex,next.tex,sizeof(a->tex)); memcpy(a->fbo,next.fbo,sizeof(a->fbo));
    a->scene_fbo=ctx->hdr_fbo; a->scene_color=ctx->hdr_color;
    memset(&next,0,sizeof(next)); ok=1;
finish:
    gl33_ao_delete_targets(&next);
    for(int i=0;i<5;i++) if(programs[i]) glDeleteProgram(programs[i]);
    gl33_ao_restore(&saved); return ok;
}
/* Called before actual draws. Sky has depth disabled and must not lock its temporary identity matrices. */
static int gl33_ao_draw_allowed(GL33Context* ctx) {
    if(!ctx->ao.on) return 1;
    if(ctx->ao.opaque_done) return ctx->blend_additive || ctx->alpha<.999f || ctx->water.on;
    if(ctx->water.on || (!ctx->blend_additive && ctx->alpha<.999f)) return 0;
    GLboolean write=GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    if(!write || !glIsEnabled(GL_DEPTH_TEST)) return 1;
    for(int i=0;i<16;i++) if(!isfinite(ctx->projection[i])||!isfinite(ctx->view[i])) return 0;
    if(ctx->ao.view_locked) {
        return memcmp(ctx->ao.projection,ctx->projection,sizeof(ctx->projection))==0 &&
               memcmp(ctx->ao.view,ctx->view,sizeof(ctx->view))==0;
    }
    memcpy(ctx->ao.projection,ctx->projection,sizeof(ctx->projection));
    memcpy(ctx->ao.view,ctx->view,sizeof(ctx->view)); ctx->ao.view_locked=1;
    return 1;
}
static void gl33_ao_pass(GL33Context* ctx,int program,int target,int w,int h,GLuint input,int axis) {
    GL33AOState* a=&ctx->ao; GLuint p=a->program[program];
    glBindFramebuffer(GL_FRAMEBUFFER,a->fbo[target]); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,w,h); glUseProgram(p);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,ctx->hdr_depth);
    glUniform1i(glGetUniformLocation(p,"uDepth"),0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,input);
    glUniform1i(glGetUniformLocation(p,"uAO"),1);
    glUniform2i(glGetUniformLocation(p,"uFullSize"),a->w,a->h);
    glUniform4f(glGetUniformLocation(p,"uProjection"),a->projection[0],a->projection[5],a->projection[10],a->projection[14]);
    glUniform1f(glGetUniformLocation(p,"uRadius"),a->radius);
    glUniform1f(glGetUniformLocation(p,"uStrength"),a->strength);
    glUniform1f(glGetUniformLocation(p,"uBias"),a->bias);
    glUniform2i(glGetUniformLocation(p,"uAxis"),axis==1,axis==2);
    glDrawArrays(GL_TRIANGLES,0,3);
}
static int gl33_opaque_end(void* v) {
    GL33Context* ctx=(GL33Context*)v;
    if(!ctx || !ctx->hdr_on) return 0;
    if(!ctx->ao.on || ctx->ao.opaque_done) return 1;
    if(!ctx->hdr_fbo || ctx->hdr_finalized || !ctx->ao.ambient) return 0;
    glfwMakeContextCurrent(ctx->window);
    GL33AOSaved saved; gl33_ao_save(&saved);
    GL33AOState* a=&ctx->ao;
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glBindVertexArray(ctx->imm_vao);
    if(a->radius<=0 || a->strength<=0 || !a->view_locked) {
        const GLfloat one[4]={1,1,1,1};
        glBindFramebuffer(GL_FRAMEBUFFER,a->fbo[2]); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearBufferfv(GL_COLOR,0,one);
    } else {
        int hw=(a->w+1)/2,hh=(a->h+1)/2;
        gl33_ao_pass(ctx,0,0,hw,hh,0,0);
        gl33_ao_pass(ctx,1,1,hw,hh,a->tex[0],1);
        gl33_ao_pass(ctx,1,0,hw,hh,a->tex[1],2);
        gl33_ao_pass(ctx,2,2,a->w,a->h,a->tex[0],0);
        glBindFramebuffer(GL_FRAMEBUFFER,a->composite_fbo); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0,0,a->w,a->h);
        glUseProgram(a->program[3]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,a->ambient);
        glUniform1i(glGetUniformLocation(a->program[3],"uAmbient"),0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,a->tex[2]);
        glUniform1i(glGetUniformLocation(a->program[3],"uAppliedAO"),1);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFunc(GL_ONE,GL_ONE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    gl33_ao_restore(&saved);
    /* Explicit phase transition: ambient is immutable after this point. */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,ctx->hdr_fbo); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    a->opaque_done=1;
    return 1;
}
static int gl33_ao_debug_active(GL33Context* ctx) {
    return ctx->ao.on && ctx->ao.debug && ctx->ao.opaque_done;
}
static void gl33_ao_debug_present(GL33Context* ctx) {
    GLuint p=ctx->ao.program[4]; glUseProgram(p);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,ctx->ao.tex[2]);
    glUniform1i(glGetUniformLocation(p,"uAppliedAO"),0);
    glBindVertexArray(ctx->imm_vao); glDrawArrays(GL_TRIANGLES,0,3); glBindVertexArray(0);
}
#endif
