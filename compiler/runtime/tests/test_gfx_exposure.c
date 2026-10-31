/* Actual GL33 metering and frame lifecycle, against independent scalar oracles. */
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include "../moo_3d_gl33.c"
static int gfx_errors;
MooValue moo_string_new(const char* s) { (void)s; return (MooValue){0}; }
static jmp_buf gfx_meter_jump;
static int gfx_meter_armed;
static unsigned char gfx_fallback_pixel;
static int gfx_expected_fallback=232;
void moo_throw(MooValue value) {
    (void)value; ++gfx_errors;
    if(gfx_meter_armed) {
        GLint fbo=-1,vao=-1,program=-1,depth_func=0;
        GLboolean depth_mask=GL_FALSE;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
        assert(fbo==0 && vao==0 && program==0 && depth_mask==GL_TRUE);
        assert(glIsEnabled(GL_BLEND) && glIsEnabled(GL_DEPTH_TEST) && depth_func==GL_LEQUAL);
        unsigned char p[4]={0}; glReadPixels(80,60,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        assert(abs((int)p[0]-gfx_expected_fallback)<=2 && p[0]==p[1] && p[1]==p[2]);
        gfx_fallback_pixel=p[0];
        longjmp(gfx_meter_jump,1);
    }
}
static void gfx_near(double got,double expected,double tolerance,const char* label) {
    if(fabs(got-expected)>tolerance) fprintf(stderr,"%s got %.9g expected %.9g\n",label,got,expected);
    assert(isfinite(got) && fabs(got-expected)<=tolerance);
    printf("PASS %s\n",label);
}
static void gfx_linear_clear(GL33Context* ctx,float value) {
    gl33_clear(ctx,0,0,0);
    glClearColor(value,value,value,1); glClear(GL_COLOR_BUFFER_BIT);
}
static void gfx_finish(GL33Context* ctx) {
    gl33_scene_end(ctx); assert(gfx_errors==0); assert(glGetError()==GL_NO_ERROR);
}
static float gfx_light_radiance(GL33Context* ctx,int slot,float intensity) {
    gl33_set_ambient(ctx,slot==0 ? 1 : 0);
    if(slot>0 && slot<4) gl33_set_extra_light(ctx,slot,0,0,1,0,0,0);
    if(slot>=4) gl33_set_point_light(ctx,slot-4,0,0,2,4,0,0,0);
    for(int j=0;j<8;j++) gl33_set_linear_light(ctx,j,0,0,0);
    gl33_set_linear_light(ctx,slot,intensity,intensity,intensity);
    gfx_linear_clear(ctx,0);
    gl33_triangle(ctx,-1,-1,0,3,-1,0,-1,3,0,1,1,1);
    float pixel[4]={0}; glReadPixels(80,60,1,1,GL_RGBA,GL_FLOAT,pixel);
    return pixel[0];
}
static PFNGLGETSHADERIVPROC gfx_real_shader_iv;
static void APIENTRY gfx_failed_shader_iv(GLuint shader,GLenum name,GLint* value) {
    gfx_real_shader_iv(shader,name,value);
    if(name==GL_COMPILE_STATUS) *value=GL_FALSE;
}
static GLenum APIENTRY gfx_failed_fbo(GLenum target) {
    (void)target; return GL_FRAMEBUFFER_UNSUPPORTED;
}
static void gfx_resource_failure(GL33Context* ctx,int shader_fault) {
    gl33_exposure_release(ctx);
    gl33_set_exposure(ctx,2);
    gl33_set_bloom(ctx,.5f,.8f,2);
    gfx_linear_clear(ctx,1); gfx_finish(ctx); /* Warm bloom targets before injecting meter faults. */
    unsigned char baseline[4]; glReadPixels(80,60,1,1,GL_RGBA,GL_UNSIGNED_BYTE,baseline);
    assert(baseline[0]>232); gfx_expected_fallback=baseline[0];
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,1); /* Scene target is already valid before injection. */
    PFNGLCHECKFRAMEBUFFERSTATUSPROC real_fbo=glad_glCheckFramebufferStatus;
    gfx_real_shader_iv=glad_glGetShaderiv;
    if(shader_fault) glad_glGetShaderiv=gfx_failed_shader_iv;
    else glad_glCheckFramebufferStatus=gfx_failed_fbo;
    double previous_time=ctx->exposure.last_time;
    gfx_meter_armed=1;
    if(setjmp(gfx_meter_jump)==0) { gl33_scene_end(ctx); assert(!"resource failure must throw"); }
    gfx_meter_armed=0;
    glad_glGetShaderiv=gfx_real_shader_iv;
    glad_glCheckFramebufferStatus=real_fbo;
    assert(gfx_errors==1 && ctx->hdr_finalized && ctx->hdr_exposure==2);
    assert(ctx->exposure.last_time==previous_time && ctx->exposure.program==0);
    gl33_scene_end(ctx); assert(gfx_errors==1); gfx_errors=0;
    gfx_linear_clear(ctx,.25f); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,.72,1e-4,"meter resource failure recovers next frame");
    printf("PASS injected meter %s failure with warm bloom matches prior-exposure frame before throw\n",shader_fault?"shader":"FBO");
    gl33_set_bloom(ctx,0,.8f,2); gfx_expected_fallback=232;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    GL33Context* ctx=(GL33Context*)gl33_create_window("Exposure oracle",161,121);
    assert(ctx); gl33_set_hdr(ctx,1); gl33_set_render_time(ctx,1,0);
    gl33_set_auto_exposure(ctx,.01f,64,.18f,2);
    gfx_near(gl33_exposure_value(ctx),1,0,"getter before first auto frame preserves prior exposure");
    gfx_linear_clear(ctx,.25f); gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,.25,1e-5,"constant odd-sized log mean");
    gfx_near(ctx->hdr_exposure,.72,1e-4,"first frame targets measured luminance");
    gl33_set_render_time(ctx,1,1);
    gfx_linear_clear(ctx,.0625f); gfx_finish(ctx);
    double first=.72+(2.88-.72)*(1-exp(-.5));
    gfx_near(ctx->hdr_exposure,first,1e-4,"linear exposure exponential adaptation");
    int w=0,h=0;
    float saved=ctx->hdr_exposure;
    uint8_t* a=gl33_grab_rgba(ctx,&w,&h); assert(a); free(a);
    gl33_scene_end(ctx);
    uint8_t* b=gl33_grab_rgba(ctx,&w,&h); assert(b); free(b);
    gfx_near(ctx->hdr_exposure,saved,0,"repeated capture and scene end never adapt");
    gl33_swap(ctx);
    gfx_near(ctx->hdr_exposure,saved,0,"presentation never repeats adaptation");
    gfx_linear_clear(ctx,1); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,saved,0,"same fixed time keeps exposure across changed scene");
    gl33_set_render_time(ctx,1,.5f);
    gfx_linear_clear(ctx,1); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,.18,1e-4,"time rollback resets to target");
    gl33_set_auto_exposure(ctx,.01f,64,.18f,0);
    gfx_linear_clear(ctx,1); gfx_finish(ctx);
    gl33_set_render_time(ctx,1,2);
    gfx_linear_clear(ctx,.0625f); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,2.88,1e-4,"tau zero targets immediately for positive dt");
    gl33_set_auto_exposure(ctx,.5f,2,.18f,1);
    gfx_linear_clear(ctx,0); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,2,0,"black uses epsilon and upper clamp");
    gl33_set_auto_exposure(ctx,.5f,2,.18f,1);
    gfx_linear_clear(ctx,16); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,.5,0,"bright field lower clamp");
    gl33_set_exposure(ctx,3);
    gfx_linear_clear(ctx,.001f); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,3,0,"manual mode disables adaptation completely");

    for(int slot=0;slot<8;slot++) {
        float low=gfx_light_radiance(ctx,slot,.1f),high=gfx_light_radiance(ctx,slot,.2f);
        assert(low>0);
        gfx_near(high/low,2,.005,"each linear light slot doubles radiance");
    }
    gl33_set_light_color(ctx,.2f,.2f,.2f);
    assert(ctx->light_linear[0]==0);
    gl33_set_extra_light(ctx,1,0,0,1,.2f,.2f,.2f);
    assert(ctx->light_linear[1]==0);
    gl33_set_point_light(ctx,0,0,0,2,4,.2f,.2f,.2f);
    assert(ctx->light_linear[4]==0);
    puts("PASS existing color setters restore their decode modes");

    /* One bright edge pixel has weight exactly 1/(161*121), no duplicated edge. */
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,.0625f);
    glEnable(GL_SCISSOR_TEST); glScissor(160,120,1,1);
    glClearColor(16,16,16,1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
    gfx_finish(ctx);
    double count=161.0*121.0;
    double geometric=exp(((count-1)*log(.0625)+log(16))/count);
    gfx_near(ctx->exposure.mean_luminance,geometric,2e-6,"single bright edge pixel weighted exactly");
    /* Unequal 80/81 columns distinguish log-before-filter from arithmetic mean. */
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,.0625f);
    glEnable(GL_SCISSOR_TEST); glScissor(80,0,81,121);
    glClearColor(4,4,4,1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
    gfx_finish(ctx);
    double mixed=exp((80.0*log(.0625)+81.0*log(4))/161.0);
    gfx_near(ctx->exposure.mean_luminance,mixed,1e-4,"log before reduction with unequal block weights");
    /* Bloom must not change measured source luminance. */
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gl33_set_bloom(ctx,.5f,.8f,2);
    gfx_linear_clear(ctx,2); gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,2,1e-4,"metering is before bloom");
    gl33_set_bloom(ctx,0,.8f,2);
    glfwSetWindowSize(ctx->window,213,137); glfwPollEvents(); gl33_swap(ctx);
    gfx_linear_clear(ctx,.25f); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,.72,1e-4,"resize resets adaptation on next presented-size frame");
    glfwSetWindowSize(ctx->window,257,257); glfwPollEvents(); gl33_swap(ctx);
    gfx_linear_clear(ctx,.25f); gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,.25,1e-5,"pixel count above half-float range stays finite");
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,0);
    glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT); gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,.2126,1e-5,"red uses Rec709 luminance weight");
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,0);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT); gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,.0722,1e-5,"blue uses Rec709 luminance weight");
    /* Faults must finish tone mapping and restore GL state BEFORE the nonlocal throw. */
    gl33_set_exposure(ctx,2);
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,1);
    GLuint good_meter=ctx->exposure.program;
    GLuint bad_meter=gl33_post_program("#version 330 core\nout vec2 meter; void main(){meter=vec2(0);}\n");
    assert(bad_meter);
    ctx->exposure.program=bad_meter;
    float previous=ctx->hdr_exposure;
    double previous_time=ctx->exposure.last_time;
    gfx_meter_armed=1;
    if(setjmp(gfx_meter_jump)==0) { gl33_scene_end(ctx); assert(!"meter failure must throw"); }
    gfx_meter_armed=0;
    assert(gfx_errors==1 && ctx->hdr_finalized && ctx->hdr_exposure==previous);
    assert(ctx->exposure.last_time==previous_time);
    gl33_scene_end(ctx);
    int fw=0,fh=0; uint8_t* fallback=gl33_grab_rgba(ctx,&fw,&fh); assert(fallback);
    assert(fallback[(60*fw+80)*4]==gfx_fallback_pixel); free(fallback);
    assert(gfx_errors==1); gfx_errors=0;
    ctx->exposure.program=good_meter; glDeleteProgram(bad_meter);
    gl33_set_render_time(ctx,1,20);
    gfx_linear_clear(ctx,.25f); gfx_finish(ctx);
    gfx_near(ctx->hdr_exposure,.72,1e-4,"meter error catch recovers on next frame");
    puts("PASS meter error tone maps previous exposure before throw; state/capture/end safe");

    gfx_resource_failure(ctx,1);
    gfx_resource_failure(ctx,0);

    /* Half the rows invalid: incorrect count inclusion causes a large mean error. */
    gl33_set_auto_exposure(ctx,.001f,64,.18f,1);
    gfx_linear_clear(ctx,.25f);
    int invalid_count=ctx->hdr_w*(ctx->hdr_h/2);
    float* invalid=malloc((size_t)invalid_count*4*sizeof(float)); assert(invalid);
    for(int i=0;i<invalid_count;i++) { invalid[4*i]=NAN; invalid[4*i+1]=INFINITY; invalid[4*i+2]=0; invalid[4*i+3]=1; }
    glBindTexture(GL_TEXTURE_2D,ctx->hdr_color);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,ctx->hdr_w,ctx->hdr_h/2,GL_RGBA,GL_FLOAT,invalid);
    free(invalid);
    glBindTexture(GL_TEXTURE_2D,0);
    gfx_finish(ctx);
    gfx_near(ctx->exposure.mean_luminance,.25,1e-5,"invalid RGB texels excluded from meter");
    GLuint tex=ctx->exposure.tex[0],fbo=ctx->exposure.fbo[0],program=ctx->exposure.program;
    gl33_hdr_release(ctx); gl33_hdr_release(ctx);
    assert(!glIsTexture(tex) && !glIsFramebuffer(fbo) && !glIsProgram(program));
    gl33_close(ctx); glfwTerminate();
    puts("GFX-EXPOSURE TARGETS PASS");
    return 0;
}
