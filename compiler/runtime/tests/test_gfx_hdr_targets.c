/* Actual GL33 implementation, deterministic fault injection at GLAD entrypoints. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d_gl33.c"

static int hdr_errors;
MooValue moo_string_new(const char* s) { (void)s; return (MooValue){0}; }
void moo_throw(MooValue value) { (void)value; ++hdr_errors; }
static GLenum APIENTRY hdr_incomplete(GLenum target) {
    (void)target; return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
}
static PFNGLGETSHADERIVPROC hdr_real_get_shader;
static void APIENTRY hdr_fail_shader(GLuint shader, GLenum pname, GLint* value) {
    if (pname == GL_COMPILE_STATUS) *value = GL_FALSE;
    else hdr_real_get_shader(shader,pname,value);
}
static void hdr_resize(GL33Context* ctx, int w, int h) {
    glfwSetWindowSize(ctx->window,w,h);
    glfwPollEvents();
    /* Exercise the next frame after normal resize/presentation. On tested GLX,
     * reading a newly enlarged BACK before presentation still sees old bounds
     * (diagnostic job e0ba0f9c); immediate transitional capture is not this gate. */
    /* Separate known-failure reproducer: GFX_HDR_RESIZE_BEFORE_PRESENT=1
     * mise run test-gfx-hdr-targets. Never enabled by the normal gate. */
    if (!getenv("GFX_HDR_RESIZE_BEFORE_PRESENT")) gl33_swap(ctx);
    int actual_w=0,actual_h=0;
    glfwGetFramebufferSize(ctx->window,&actual_w,&actual_h);
    assert(actual_w>0 && actual_h>0);
}
static void hdr_pixel(GL33Context* ctx,int expected,const char* label) {
    int w=0,h=0,fw=0,fh=0;
    uint8_t* data=gl33_grab_rgba(ctx,&w,&h);
    glfwGetFramebufferSize(ctx->window,&fw,&fh);
    assert(data && w==fw && h==fh);
    size_t points[]={0,((size_t)h/2*(size_t)w+(size_t)w/2)*4,((size_t)w*h-1)*4};
    for (int i=0;i<3;++i)
        for(int c=0;c<3;++c) {
            int got=data[points[i]+(size_t)c];
            if(abs(got-expected)>2) fprintf(stderr,"%s: point %d channel %d dimensions %dx%d got %d expected %d GLerr=%u\n",label,i,c,w,h,got,expected,(unsigned)glGetError());
            assert(abs(got-expected)<=2);
        }
    free(data);
    assert(glGetError()==GL_NO_ERROR);
    printf("PASS %s\n",label);
}
static void hdr_triangle(GL33Context* ctx,float color) {
    gl33_triangle(ctx,-1,-1,0,3,-1,0,-1,3,0,color,color,color);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    GL33Context* ctx=(GL33Context*)gl33_create_window("HDR targets",161,121);
    assert(ctx);
    gl33_set_hdr(ctx,1);
    assert(hdr_errors==0 && ctx->hdr_on && ctx->hdr_exposure==1.0f);
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,0,0);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE); glDepthMask(GL_FALSE);
    glViewport(3,7,8,8);
    gl33_clear(ctx,1,1,1);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    hdr_pixel(ctx,206,"clear establishes full pixel viewport/masks/scissor");
    gl33_clear(ctx,0,0,0);
    gl33_set_ambient(ctx,1);
    gl33_triangle(ctx,-1,-1,0,3,-1,0,-1,3,0,1,1,1);
    hdr_pixel(ctx,206,"actual white geometry");
    gl33_clear(ctx,0,0,0);
    hdr_triangle(ctx,.5f);
    hdr_pixel(ctx,103,"vertex sRGB decode before interpolation");
    gl33_clear(ctx,0,0,0); gl33_set_tint(ctx,.5f,.5f,.5f);
    hdr_triangle(ctx,1);
    hdr_pixel(ctx,103,"tint sRGB decode once");
    gl33_set_tint(ctx,1,1,1);
    const uint8_t texel[4]={128,128,128,255};
    int tex=gl33_texture_create(ctx,texel,1,1);
    assert(tex>0);
    gl33_clear(ctx,0,0,0); gl33_set_texture(ctx,tex,1);
    hdr_triangle(ctx,1);
    hdr_pixel(ctx,104,"texture sRGB decode before filter");
    gl33_set_texture(ctx,0,1);
    gl33_set_light_color(ctx,0,0,0); gl33_set_emissive(ctx,2);
    gl33_clear(ctx,0,0,0); hdr_triangle(ctx,1);
    hdr_pixel(ctx,232,"linear radiance above one retained");
    gl33_set_bloom(ctx,.5f,.8f,2);
    gl33_clear(ctx,0,0,0); hdr_triangle(ctx,1);
    hdr_pixel(ctx,240,"float bloom before tonemap retains radiance three");
    gl33_set_bloom(ctx,0,.8f,2); gl33_set_emissive(ctx,0);
    gl33_set_light_color(ctx,1,1,1);

    hdr_resize(ctx,213,137);
    gl33_clear(ctx,1,1,1);
    int w=0,h=0;
    glfwGetFramebufferSize(ctx->window,&w,&h);
    assert(ctx->hdr_w==w && ctx->hdr_h==h);
    gl33_perspective(ctx,60,.1f,100);
    assert(fabsf(ctx->projection[5]/ctx->projection[0]-(float)w/h)<1e-5f);
    glBindTexture(GL_TEXTURE_2D,ctx->hdr_color);
    GLint tw=0,th=0,format=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&th);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
    assert(tw==w && th==h && format==GL_RGBA16F);
    float scene_pixel[4]={0};
    glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_FLOAT,scene_pixel);
    fprintf(stderr,"resize scene %.4f %.4f %.4f; target %dx%d errors=%d\n",scene_pixel[0],scene_pixel[1],scene_pixel[2],w,h,hdr_errors);
    assert(fabsf(scene_pixel[0]-1.0f)<.001f);
    hdr_pixel(ctx,206,"post-presentation resize frame uses full framebuffer pixel dimensions");
    GLuint keep_fbo=ctx->hdr_fbo,keep_color=ctx->hdr_color,keep_depth=ctx->hdr_depth;
    int errors=hdr_errors;
    assert(!gl33_hdr_targets(ctx,0,0));
    assert(hdr_errors==errors+1 && ctx->hdr_fbo==keep_fbo && ctx->hdr_color==keep_color);
    puts("PASS zero-sized target rejected without allocation");

    hdr_resize(ctx,191,129);
    PFNGLCHECKFRAMEBUFFERSTATUSPROC check=glad_glCheckFramebufferStatus;
    glad_glCheckFramebufferStatus=hdr_incomplete;
    gl33_clear(ctx,1,1,1);
    glad_glCheckFramebufferStatus=check;
    assert(hdr_errors==errors+2 && ctx->hdr_finalized);
    assert(ctx->hdr_fbo==keep_fbo && glIsFramebuffer(keep_fbo));
    assert(glIsTexture(keep_color) && glIsTexture(keep_depth));
    assert(!gl33_scene_draw_allowed(ctx));
    assert(hdr_errors==errors+3);
    gl33_clear(ctx,1,1,1);
    hdr_pixel(ctx,206,"failed FBO blocks draws and recovers on next clear");

    hdr_resize(ctx,173,127);
    glDeleteProgram(ctx->hdr_program); ctx->hdr_program=0;
    hdr_real_get_shader=glad_glGetShaderiv;
    glad_glGetShaderiv=hdr_fail_shader;
    gl33_clear(ctx,1,1,1);
    glad_glGetShaderiv=hdr_real_get_shader;
    assert(ctx->hdr_finalized && ctx->hdr_program==0);
    assert(hdr_errors==errors+4);
    gl33_clear(ctx,1,1,1);
    hdr_pixel(ctx,206,"shader failure recovers without corrupting scene owner");
    GLuint f=ctx->hdr_fbo,c=ctx->hdr_color,d=ctx->hdr_depth;
    gl33_hdr_release(ctx);
    gl33_hdr_release(ctx);
    assert(!glIsFramebuffer(f) && !glIsTexture(c) && !glIsTexture(d));
    puts("PASS HDR target release idempotent");
    gl33_close(ctx);
    glfwTerminate();
    puts("GFX-HDR TARGETS PASS");
    return 0;
}
