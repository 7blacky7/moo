/* Opt-in HDR scene, included after GL33Context. */
#ifndef MOO_3D_HDR_GL33_H
#define MOO_3D_HDR_GL33_H
static GLuint gl33_post_program(const char*);
static void gl33_render_bloom(GL33Context*);
static void gl33_upload_lighting(GL33Context*);
static void gl33_water_destroy(GL33Context*);
static int gl33_set_water(void*, const MooWaterSettings*);

static float gl33_srgb_decode(float x) {
    return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
}
static int gl33_hdr_error(const char* message) {
    moo_throw(moo_string_new(message));
    return 0;
}
#include "moo_3d_exposure_gl33.h"
static int gl33_scene_draw_allowed(GL33Context* ctx) {
    if (!ctx) return 0;
    if (ctx->hdr_on && (ctx->hdr_finalized || !ctx->hdr_fbo))
        return gl33_hdr_error("HDR: Szene mit clear beginnen; nach raum_szene_ende kein Szenen-Draw");
    if (!gl33_ao_draw_allowed(ctx))
        return gl33_hdr_error("AO: ungueltige Zeichenphase oder geaenderte Tiefenkamera");
    ctx->hdr_resources_started = 1;
    return 1;
}
/* Stephen Hill ACES fitted, from MJP BakingLab/ACES.hlsl (MIT).
 * https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
 * Copyright (c) 2016 MJP. Permission is hereby granted, free of charge, to any
 * person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
static const char* GL33_HDR_FS =
    "#version 330 core\n"
    "in vec2 vUV; uniform sampler2D uSrc; uniform float uExposure; out vec4 fragColor;\n"
    "void main() {\n"
    " vec3 c=clamp(texture(uSrc,vUV).rgb,vec3(0),vec3(65504))*uExposure;\n"
    " mat3 a=mat3(.59719,.07600,.02840, .35458,.90834,.13383, .04823,.01566,.83777);\n"
    " mat3 b=mat3(1.60475,-.10208,-.00327, -.53108,1.10813,-.07276, -.07367,-.00605,1.07602);\n"
    " vec3 v=a*c; v=(v*(v+.0245786)-.000090537)/(v*(.983729*v+.4329510)+.238081);\n"
    " v=clamp(b*v,vec3(0),vec3(1));\n"
    " v=mix(1.055*pow(v,vec3(1.0/2.4))-.055,12.92*v,lessThanEqual(v,vec3(.0031308)));\n"
    " fragColor=vec4(v,1);\n"
    "}\n";

void gl33_hdr_release(void* vctx) {
    GL33Context* ctx=(GL33Context*)vctx;
    if (!ctx) return;
    gl33_ao_release(ctx);
    gl33_water_destroy(ctx);
    gl33_exposure_release(ctx);
    if (ctx->hdr_fbo) glDeleteFramebuffers(1,&ctx->hdr_fbo);
    if (ctx->hdr_color) glDeleteTextures(1,&ctx->hdr_color);
    if (ctx->hdr_depth) glDeleteTextures(1,&ctx->hdr_depth);
    if (ctx->hdr_program) glDeleteProgram(ctx->hdr_program);
    ctx->hdr_fbo=ctx->hdr_color=ctx->hdr_depth=ctx->hdr_program=0;
    ctx->hdr_w=ctx->hdr_h=0;
}
static int gl33_hdr_targets(GL33Context* ctx,int w,int h) {
    if (w<=0 || h<=0) return gl33_hdr_error("HDR: Framebuffer hat keine Pixel");
    if (ctx->hdr_fbo && ctx->hdr_w==w && ctx->hdr_h==h) return 1;
    if (!ctx->hdr_program) ctx->hdr_program=gl33_post_program(GL33_HDR_FS);
    if (!ctx->hdr_program) return gl33_hdr_error("HDR: Tonemap-Shader fehlgeschlagen");
    GLuint f=0,c=0,d=0;
    glGenFramebuffers(1,&f); glBindFramebuffer(GL_FRAMEBUFFER,f);
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&c); glBindTexture(GL_TEXTURE_2D,c);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,c,0);
    glGenTextures(1,&d); glBindTexture(GL_TEXTURE_2D,d);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,w,h,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,d,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDeleteFramebuffers(1,&f); glDeleteTextures(1,&c); glDeleteTextures(1,&d);
        return gl33_hdr_error("HDR: Scene-Framebuffer unvollstaendig");
    }
    if (ctx->hdr_fbo) glDeleteFramebuffers(1,&ctx->hdr_fbo);
    if (ctx->hdr_color) glDeleteTextures(1,&ctx->hdr_color);
    if (ctx->hdr_depth) glDeleteTextures(1,&ctx->hdr_depth);
    ctx->hdr_fbo=f; ctx->hdr_color=c; ctx->hdr_depth=d;
    ctx->hdr_w=w; ctx->hdr_h=h;
    return 1;
}
static void gl33_set_hdr(void* vctx,int on) {
    GL33Context* ctx=(GL33Context*)vctx;
    if (!ctx || ctx->hdr_on==on) return;
    if (ctx->hdr_resources_started) { gl33_hdr_error("HDR: Modus vor Ressourcenbau/erster Szene waehlen"); return; }
    glfwMakeContextCurrent(ctx->window);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    GLint depth=0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH,
        GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth);
    if (on && depth<=0) { gl33_hdr_error("HDR: finaler Framebuffer ohne Overlay-Depth"); return; }
    ctx->hdr_on=on;
    if (!on) { ctx->pbr[0]=0; ctx->pbr[1]=0; ctx->pbr[2]=1; gl33_ao_reset_mode(ctx); }
    ctx->bloom_w=ctx->bloom_h=0;
    glUseProgram(ctx->program);
    glUniform1i(glGetUniformLocation(ctx->program,"uHDR"),on);
    gl33_upload_lighting(ctx);
}
static void gl33_set_exposure(void* vctx,float exposure) {
    GL33Context* ctx=(GL33Context*)vctx;
    if (!ctx || !ctx->hdr_on) { gl33_hdr_error("Belichtung erfordert HDR"); return; }
    ctx->hdr_exposure=exposure;
    ctx->exposure.on=0; ctx->exposure.initialized=0;
}
static void gl33_scene_end(void* vctx) {
    GL33Context* ctx=(GL33Context*)vctx;
    if (!ctx || !ctx->hdr_on || ctx->hdr_finalized) return;
    if (!ctx->hdr_fbo) { gl33_hdr_error("HDR: Szene zuerst mit clear beginnen"); return; }
    if (ctx->ao.on && !ctx->ao.opaque_done) {
        gl33_hdr_error("AO: raum_opak_ende vor Szenenabschluss erforderlich"); return;
    }
    glfwMakeContextCurrent(ctx->window);
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,ctx->hdr_w,ctx->hdr_h);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    ctx->hdr_finalized=1; /* Metering errors are deferred until the frame is safe. */
    int exposure_ok=gl33_exposure_update(ctx);
    /* Target creation failures can leave framebuffer 0 bound; bloom must read the scene. */
    glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,ctx->hdr_w,ctx->hdr_h);
    if (!ctx->bloom_done) { gl33_render_bloom(ctx); ctx->bloom_done=1; }
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    glViewport(0,0,ctx->hdr_w,ctx->hdr_h);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    if (gl33_ao_debug_active(ctx)) {
        gl33_ao_debug_present(ctx);
    } else {
        glUseProgram(ctx->hdr_program);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,ctx->hdr_color);
        glUniform1i(glGetUniformLocation(ctx->hdr_program,"uSrc"),0);
        glUniform1f(glGetUniformLocation(ctx->hdr_program,"uExposure"),ctx->hdr_exposure);
        glBindVertexArray(ctx->imm_vao); glDrawArrays(GL_TRIANGLES,0,3);
    }
    glBindVertexArray(0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,0);
    /* Overlay owns final depth, cleared exactly once per scene. */
    glDepthMask(GL_TRUE); glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
    ctx->hdr_finalized=1;
    if (!exposure_ok) gl33_hdr_error("Belichtung: Messung fehlgeschlagen; vorheriger Wert verwendet");
}
int gl33_hdr_enabled(void* vctx) {
    return vctx && ((GL33Context*)vctx)->hdr_on;
}
void gl33_overlay_begin(void* vctx) {
    GL33Context* ctx=(GL33Context*)vctx;
    if (!ctx || !ctx->hdr_on) return;
    glfwMakeContextCurrent(ctx->window);
    gl33_scene_end(ctx);
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDrawBuffer(GL_BACK);
    glViewport(0,0,ctx->hdr_w,ctx->hdr_h);
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
#endif
