/* Isolated actual-GLSL AO gates; no moo runtime or shared render state linked. */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../moo_3d_ao_glsl.h"

typedef struct { GLuint texture,fbo; int w,h; } GfxAOTarget;
static GLuint gfx_ao_vao, gfx_ao_programs[5];
static const float gfx_ao_projection[4]={1.3f,1.7f,-100.1f/99.9f,-20.0f/99.9f};
static GLuint gfx_ao_shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type); glShaderSource(s,1,&source,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[8192]; glGetShaderInfoLog(s,sizeof(log),NULL,log); fprintf(stderr,"%s\n",log); abort(); }
    return s;
}
static GLuint gfx_ao_program(const char *fragment) {
    const char *vertex="#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}";
    GLuint vs=gfx_ao_shader(GL_VERTEX_SHADER,vertex),fs=gfx_ao_shader(GL_FRAGMENT_SHADER,fragment);
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) { char log[8192]; glGetProgramInfoLog(p,sizeof(log),NULL,log); fprintf(stderr,"%s\n",log); abort(); }
    glDeleteShader(vs); glDeleteShader(fs); return p;
}
static GLuint gfx_ao_texture(int w,int h,GLint storage,GLenum format,const float *data) {
    GLuint t=0; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,storage,w,h,0,format,GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t;
}
static GfxAOTarget gfx_ao_target(int w,int h,GLint storage,GLenum format,const float *data) {
    GfxAOTarget t={gfx_ao_texture(w,h,storage,format,data),0,w,h};
    glGenFramebuffers(1,&t.fbo); glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE); return t;
}
static void gfx_ao_free_target(GfxAOTarget t) { glDeleteFramebuffers(1,&t.fbo); glDeleteTextures(1,&t.texture); }
static void gfx_ao_sampler(GLuint program,const char *name,int unit,GLuint texture) {
    glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,texture);
    glUniform1i(glGetUniformLocation(program,name),unit);
}
static void gfx_ao_begin(GLuint program,GfxAOTarget target) {
    glBindFramebuffer(GL_FRAMEBUFFER,target.fbo); glViewport(0,0,target.w,target.h);
    glUseProgram(program); glBindVertexArray(gfx_ao_vao);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
}
static void gfx_ao_uniforms(GLuint p,GLuint depth,int w,int h,float radius,float strength) {
    gfx_ao_sampler(p,"uDepth",0,depth);
    glUniform2i(glGetUniformLocation(p,"uFullSize"),w,h);
    glUniform4fv(glGetUniformLocation(p,"uProjection"),1,gfx_ao_projection);
    glUniform1f(glGetUniformLocation(p,"uRadius"),radius);
    glUniform1f(glGetUniformLocation(p,"uStrength"),strength);
    glUniform1f(glGetUniformLocation(p,"uBias"),.02f);
}
static void gfx_ao_pipeline(GLuint depth,int w,int h,float radius,float strength,
                            GfxAOTarget a,GfxAOTarget b,GfxAOTarget applied) {
    GLuint p=gfx_ao_programs[0]; gfx_ao_begin(p,a); gfx_ao_uniforms(p,depth,w,h,radius,strength);
    glDrawArrays(GL_TRIANGLES,0,3);
    p=gfx_ao_programs[1]; gfx_ao_begin(p,b); gfx_ao_uniforms(p,depth,w,h,radius,strength);
    gfx_ao_sampler(p,"uAO",1,a.texture); glUniform2i(glGetUniformLocation(p,"uAxis"),1,0);
    glDrawArrays(GL_TRIANGLES,0,3);
    gfx_ao_begin(p,a); gfx_ao_uniforms(p,depth,w,h,radius,strength);
    gfx_ao_sampler(p,"uAO",1,b.texture); glUniform2i(glGetUniformLocation(p,"uAxis"),0,1);
    glDrawArrays(GL_TRIANGLES,0,3);
    p=gfx_ao_programs[2]; gfx_ao_begin(p,applied); gfx_ao_uniforms(p,depth,w,h,radius,strength);
    gfx_ao_sampler(p,"uAO",1,a.texture); glDrawArrays(GL_TRIANGLES,0,3);
}
static float gfx_ao_depth(float positiveZ) {
    return .5f*(1.0f-gfx_ao_projection[2]+gfx_ao_projection[3]/positiveZ);
}
static void gfx_ao_depth_cases(int w,int h) {
    size_t count=(size_t)w*(size_t)h;
    float *depth=malloc(count*sizeof(float)),*first=malloc(count*sizeof(float)),*second=malloc(count*sizeof(float));
    assert(depth && first && second);
    GfxAOTarget a=gfx_ao_target((w+1)/2,(h+1)/2,GL_R16F,GL_RED,NULL);
    GfxAOTarget b=gfx_ao_target(a.w,a.h,GL_R16F,GL_RED,NULL);
    GfxAOTarget applied=gfx_ao_target(w,h,GL_R16F,GL_RED,NULL);
    for(int mode=0;mode<6;mode++) {
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            float z=4.0f;
            if(mode==1) z=4.0f/(1.0f-.25f*((2.0f*(x+.5f)/w-1.0f)/gfx_ao_projection[0]));
            if(mode>=2 && x<w/2) z=2.0f;
            depth[(size_t)y*w+x]=gfx_ao_depth(z);
            if(mode==5) depth[(size_t)y*w+x]=x%3==0?1.0f:(x%3==1?NAN:INFINITY);
        }
        GLuint d=gfx_ao_texture(w,h,GL_R32F,GL_RED,depth);
        float radius=mode==3?0.0f:3.0f, strength=mode==4?0.0f:1.0f;
        gfx_ao_pipeline(d,w,h,radius,strength,a,b,applied);
        glReadPixels(0,0,w,h,GL_RED,GL_FLOAT,first);
        gfx_ao_pipeline(d,w,h,radius,strength,a,b,applied);
        glReadPixels(0,0,w,h,GL_RED,GL_FLOAT,second);
        assert(memcmp(first,second,count*sizeof(float))==0);
        float minimum=1.0f;
        for(size_t i=0;i<count;i++) {
            assert(isfinite(first[i]) && first[i]>=0.0f && first[i]<=1.0f);
            if(first[i]<minimum) minimum=first[i];
            if(mode!=2) assert(first[i]==1.0f);
        }
        if(mode==2 && w>=17 && h>=11) assert(minimum<.99f);
        printf("PASS AO depth %dx%d mode=%d min=%.7f deterministic\n",w,h,mode,minimum);
        glDeleteTextures(1,&d);
    }
    gfx_ao_free_target(a); gfx_ao_free_target(b); gfx_ao_free_target(applied);
    free(depth); free(first); free(second);
}
static void gfx_ao_composition(void) {
    float ambient[4]={.1f,.2f,.3f,1},scene[4]={.30f,.50f,.70f,.7f},factor=.5f;
    GLuint a=gfx_ao_texture(1,1,GL_RGBA16F,GL_RGBA,ambient);
    GLuint ao=gfx_ao_texture(1,1,GL_R16F,GL_RED,&factor);
    GfxAOTarget hdr=gfx_ao_target(1,1,GL_RGBA16F,GL_RGBA,scene);
    GLuint p=gfx_ao_programs[3]; gfx_ao_begin(p,hdr);
    gfx_ao_sampler(p,"uAmbient",0,a); gfx_ao_sampler(p,"uAppliedAO",1,ao);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFunc(GL_ONE,GL_ONE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE); glDrawArrays(GL_TRIANGLES,0,3);
    float result[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result);
    const float expected[4]={.25f,.40f,.55f,.7f};
    for(int i=0;i<4;i++) assert(fabsf(result[i]-expected[i])<.001f);
    printf("PASS AO ambient-only composite %.7f %.7f %.7f alpha=%.7f\n",result[0],result[1],result[2],result[3]);
    GfxAOTarget display=gfx_ao_target(1,1,GL_RGBA8,GL_RGBA,NULL);
    p=gfx_ao_programs[4]; gfx_ao_begin(p,display); gfx_ao_sampler(p,"uAppliedAO",0,ao);
    glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char pixel[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==128 && pixel[1]==128 && pixel[2]==128 && pixel[3]==255);
    puts("PASS AO debug uses applied factor .5 -> code128");
    glDeleteTextures(1,&a); glDeleteTextures(1,&ao);
    gfx_ao_free_target(hdr); gfx_ao_free_target(display);
}
int main(void) {
    assert(glfwInit());
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window=glfwCreateWindow(33,25,"AO isolated gates",NULL,NULL); assert(window);
    glfwMakeContextCurrent(window);
    printf("Renderer: %s; performance N/A\n",glGetString(GL_RENDERER));
    glGenVertexArrays(1,&gfx_ao_vao);
    const char *sources[]={GL33_AO_CALCULATE_FS,GL33_AO_FILTER_FS,GL33_AO_UPSAMPLE_FS,GL33_AO_COMPOSITE_FS,GL33_AO_DEBUG_FS};
    for(int i=0;i<5;i++) gfx_ao_programs[i]=gfx_ao_program(sources[i]);
    gfx_ao_depth_cases(1,1); gfx_ao_depth_cases(3,5);
    gfx_ao_depth_cases(17,11); gfx_ao_depth_cases(33,25);
    gfx_ao_composition();
    assert(glGetError()==GL_NO_ERROR);
    for(int i=0;i<5;i++) glDeleteProgram(gfx_ao_programs[i]);
    glDeleteVertexArrays(1,&gfx_ao_vao); glfwDestroyWindow(window); glfwTerminate();
    puts("GFX-AO isolated actual GLSL PASS"); return 0;
}
