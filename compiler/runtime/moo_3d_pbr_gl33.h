#ifndef MOO_3D_PBR_GL33_H
#define MOO_3D_PBR_GL33_H
static void gl33_set_pbr(void* vctx,int on,float metallic,float roughness) {
    GL33Context* ctx=(GL33Context*)vctx;
    if(!ctx || !ctx->hdr_on) { gl33_hdr_error("PBR erfordert HDR"); return; }
    glfwMakeContextCurrent(ctx->window);
    ctx->pbr[0]=on ? 1.0f : 0.0f;
    ctx->pbr[1]=metallic;
    ctx->pbr[2]=fmaxf(.05f,roughness);
    glUseProgram(ctx->program);
    glUniform3fv(ctx->uniforms.pbr,1,ctx->pbr);
}
#endif
