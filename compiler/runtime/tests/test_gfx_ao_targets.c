/* Actual production context; fault injection and numerical attachment oracles. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d_gl33.c"
static int ao_errors;
MooValue moo_string_new(const char* s) { (void)s; return (MooValue){0}; }
void moo_throw(MooValue v) { (void)v; ++ao_errors; }
static GLenum APIENTRY ao_incomplete(GLenum t) { (void)t; return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT; }
static PFNGLCHECKFRAMEBUFFERSTATUSPROC ao_real_check;
static int ao_checks;
static GLenum APIENTRY ao_bad_mrt(GLenum target) {
    if(++ao_checks==5) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    return ao_real_check(target);
}
static PFNGLGETSHADERIVPROC ao_real_shader;
static void APIENTRY ao_bad_shader(GLuint shader,GLenum pname,GLint* value) {
    if(pname==GL_COMPILE_STATUS) *value=GL_FALSE; else ao_real_shader(shader,pname,value);
}
static void ao_plane(GL33Context* ctx) {
    gl33_triangle(ctx,-20,-20,-3,20,-20,-3,0,20,-3,1,1,1);
}
static void ao_setup(GL33Context* ctx,float ambient) {
    gl33_set_alpha(ctx,1); gl33_set_blend_mode(ctx,0);
    gl33_set_ambient(ctx,ambient); gl33_set_light_color(ctx,1,1,1);
    gl33_set_light_dir(ctx,0,0,1); gl33_set_spec(ctx,0,1);
    gl33_set_emissive(ctx,.2f); gl33_set_fog_density(ctx,0);
    gl33_clear(ctx,.2f,.3f,.4f); gl33_perspective(ctx,60,.1f,100);
    gl33_camera(ctx,0,0,0,0,0,-1);
    ao_plane(ctx);
}
static void ao_read(GL33Context* ctx,GLenum buffer,float* out) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,ctx->hdr_fbo); glReadBuffer(buffer);
    glReadPixels(0,0,ctx->hdr_w,ctx->hdr_h,GL_RGBA,GL_FLOAT,out);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
}
static void ao_oracle(GL33Context* ctx,float ambient) {
    int w=ctx->hdr_w,h=ctx->hdr_h; size_t n=(size_t)w*h;
    float* before=malloc(n*4*sizeof(float)); float* basis=malloc(n*4*sizeof(float));
    float* after=malloc(n*4*sizeof(float)); float* factor=malloc(n*sizeof(float));
    float* depth=malloc(n*sizeof(float)); assert(before&&basis&&after&&factor&&depth);
    ao_read(ctx,GL_COLOR_ATTACHMENT0,before); ao_read(ctx,GL_COLOR_ATTACHMENT1,basis);
    assert(fabsf(basis[(n/2)*4]-ambient)<.001f);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        float z=x<w/2?3.0f:4.0f;
        depth[(size_t)y*w+x]=.5f*(ctx->projection[14]/z-ctx->projection[10]+1);
    }
    glBindTexture(GL_TEXTURE_2D,ctx->hdr_depth);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
    glBindTexture(GL_TEXTURE_2D,0);
    GL33AOSaved before_state,after_state;
    gl33_ao_save(&before_state); assert(gl33_opaque_end(ctx)); gl33_ao_save(&after_state);
    assert(before_state.program==after_state.program && before_state.vao==after_state.vao);
    assert(before_state.depth==after_state.depth && before_state.depth_mask==after_state.depth_mask);
    assert(before_state.blend==after_state.blend && before_state.eq_rgb==after_state.eq_rgb);
    assert(before_state.active==after_state.active);
    assert(memcmp(before_state.texture,after_state.texture,sizeof(before_state.texture))==0);
    assert(memcmp(before_state.viewport,after_state.viewport,sizeof(before_state.viewport))==0);
    assert(after_state.draw==(GLint)ctx->hdr_fbo && after_state.buffers[1]==GL_NONE);
    ao_read(ctx,GL_COLOR_ATTACHMENT0,after);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,ctx->ao.fbo[2]); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,w,h,GL_RED,GL_FLOAT,factor);
    float minimum=1; int darkened=0;
    for(size_t i=0;i<n;i++) {
        assert(isfinite(factor[i]) && factor[i]>=0 && factor[i]<=1);
        if(factor[i]<minimum) minimum=factor[i];
        for(int c=0;c<3;c++) {
            float expected=before[4*i+c]-(1-factor[i])*basis[4*i+c];
            assert(fabsf(after[4*i+c]-expected)<.002f);
            if(after[4*i+c]<before[4*i+c]-.002f) darkened++;
            if(ambient==0) assert(after[4*i+c]==before[4*i+c]);
        }
        assert(after[4*i+3]==before[4*i+3]);
    }
    assert(minimum<.99f && (ambient==0 || darkened>0));
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo); glReadBuffer(GL_COLOR_ATTACHMENT0);
    assert(gl33_opaque_end(ctx));
    ao_read(ctx,GL_COLOR_ATTACHMENT0,before);
    assert(memcmp(before,after,n*4*sizeof(float))==0);
    gl33_scene_end(ctx); assert(gl33_opaque_end(ctx));
    assert(glGetError()==GL_NO_ERROR);
    printf("PASS actual AO ambient=%g min=%g correction/alpha/state/idempotence\n",ambient,minimum);
    free(before); free(basis); free(after); free(factor); free(depth);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    GL33Context* ctx=gl33_create_window("AO targets",65,49); assert(ctx);
    gl33_set_hdr(ctx,1); assert(gl33_set_ao(ctx,1,2,1,.01f));
    /* First allocation shader failure stays closed; retry owns no leaked programs. */
    ao_real_shader=glad_glGetShaderiv;
    /* Warm HDR shader first so the injected fault belongs to AO. */
    ctx->hdr_program=gl33_post_program(GL33_HDR_FS); assert(ctx->hdr_program);
    glad_glGetShaderiv=ao_bad_shader;
    gl33_clear(ctx,0,0,0); glad_glGetShaderiv=ao_real_shader;
    assert(ctx->hdr_finalized && !ctx->ao.program[0] && ao_errors==1);
    ao_setup(ctx,.4f); assert(!ctx->hdr_finalized && ctx->ao.ambient);
    ao_oracle(ctx,.4f);
    ao_setup(ctx,0); ao_oracle(ctx,0);
    gl33_set_pbr(ctx,1,0,.5f); ao_setup(ctx,.4f); ao_oracle(ctx,.4f);
    gl33_set_pbr(ctx,1,1,.5f); ao_setup(ctx,.4f); ao_oracle(ctx,0);
    gl33_set_pbr(ctx,0,0,1);
    ao_setup(ctx,.4f);
    gl33_clear(ctx,0,0,0); gl33_perspective(ctx,60,.1f,100);
    gl33_camera(ctx,0,0,0,0,0,-1);
    gl33_set_fog_density(ctx,.2f); gl33_set_fog_color(ctx,.5f,.5f,.5f);
    assert(gl33_set_height_fog(ctx,1,0,0,0)); ao_plane(ctx);
    float fogged[4]; glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(ctx->hdr_w/2,ctx->hdr_h/2,1,1,GL_RGBA,GL_FLOAT,fogged);
    float fog_t=expf(-.2f*3), fog_source=gl33_srgb_decode(.5f);
    assert(fabsf(fogged[0]-(1.2f*fog_t+fog_source*(1-fog_t)))<.002f);
    ao_oracle(ctx,.4f*fog_t);
    assert(gl33_set_height_fog(ctx,0,0,0,0)); gl33_set_fog_density(ctx,0);
    puts("PASS GGX neutral ambient / metal-zero / height-fog A*T and preserved inscatter");
    /* Sky must not lock identity projection; real depth geometry locks the perspective. */
    gl33_clear(ctx,0,0,0); gl33_perspective(ctx,55,.1f,100);
    gl33_sky(ctx,.1f,.2f,.3f,.3f,.4f,.5f); assert(!ctx->ao.view_locked);
    ao_plane(ctx); assert(ctx->ao.view_locked);
    int errors=ao_errors; gl33_perspective(ctx,60,.1f,100); ao_plane(ctx);
    assert(ao_errors==errors+1);
    gl33_perspective(ctx,55,.1f,100); assert(gl33_opaque_end(ctx)); gl33_scene_end(ctx);
    puts("PASS sky exception and depth-camera drift guard");
    /* Clear recovery from a missing opaque boundary, without finalizing on failure. */
    gl33_clear(ctx,0,0,0); errors=ao_errors; gl33_scene_end(ctx);
    assert(ao_errors==errors+1 && !ctx->hdr_finalized && !ctx->ao.opaque_done);
    assert(!gl33_water_background(ctx)); assert(!gl33_set_ao(ctx,0,0,0,0));
    assert(gl33_opaque_end(ctx)); gl33_scene_end(ctx);
    puts("PASS phase errors preserve open frame and recover");
    GLuint old=ctx->ao.ambient,old_applied=ctx->ao.tex[2];
    /* New HDR ownership with same dimensions must also reattach/rebuild AO. */
    ctx->hdr_w=0; assert(gl33_hdr_targets(ctx,65,49));
    PFNGLCHECKFRAMEBUFFERSTATUSPROC check=glad_glCheckFramebufferStatus;
    glad_glCheckFramebufferStatus=ao_incomplete;
    assert(!gl33_ao_targets(ctx)); glad_glCheckFramebufferStatus=check;
    assert(ctx->ao.ambient==old && glIsTexture(old));
    /* Attach previous storage to exercise exact fifth-check rollback. */
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,old,0);
    ao_checks=0; ao_real_check=check; glad_glCheckFramebufferStatus=ao_bad_mrt;
    assert(!gl33_ao_targets(ctx)); glad_glCheckFramebufferStatus=check;
    GLint restored=0;
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&restored);
    assert(ao_checks==5 && restored==(GLint)old && glIsTexture(old));
    assert(gl33_ao_targets(ctx) && ctx->ao.scene_fbo==ctx->hdr_fbo);
    assert(!glIsTexture(old) && !glIsTexture(old_applied));
    puts("PASS FBO failure rollback and same-size scene ownership recovery");
    gl33_clear(ctx,0,0,0); assert(gl33_opaque_end(ctx)); gl33_scene_end(ctx);
    glfwSetWindowSize(ctx->window,79,53); glfwPollEvents(); gl33_swap(ctx);
    gl33_clear(ctx,0,0,0); assert(ctx->ao.w==ctx->hdr_w && ctx->ao.h==ctx->hdr_h);
    glBindTexture(GL_TEXTURE_2D,ctx->ao.tex[0]); GLint hw=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&hw);
    assert(hw==(ctx->hdr_w+1)/2);
    assert(gl33_opaque_end(ctx)); gl33_scene_end(ctx);
    GLuint a=ctx->ao.ambient,t=ctx->ao.tex[2],p=ctx->ao.program[0],f=ctx->ao.composite_fbo;
    gl33_hdr_release(ctx); gl33_hdr_release(ctx);
    assert(!glIsTexture(a)&&!glIsTexture(t)&&!glIsProgram(p)&&!glIsFramebuffer(f));
    assert(glGetError()==GL_NO_ERROR);
    gl33_close(ctx); glfwTerminate();
    puts("GFX-AO TARGETS PASS");
    return 0;
}
