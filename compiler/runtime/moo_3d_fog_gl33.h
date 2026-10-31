#ifndef MOO_3D_FOG_GL33_H
#define MOO_3D_FOG_GL33_H
/* Included after GL33Context. No allocations or separately owned resources. */
static void gl33_fog_upload(GL33Context* ctx) {
    if (!ctx->height_fog.locations_ready) {
        ctx->height_fog.on_loc = glGetUniformLocation(ctx->program,"uHeightFogOn");
        ctx->height_fog.additive_loc = glGetUniformLocation(ctx->program,"uFogAdditive");
        ctx->height_fog.profile_loc = glGetUniformLocation(ctx->program,"uFogProfile");
        ctx->height_fog.locations_ready = 1;
    }
    glUniform1i(ctx->height_fog.on_loc,
                ctx->height_fog.on && ctx->hdr_on && !ctx->hdr_finalized);
    glUniform1i(ctx->height_fog.additive_loc,ctx->blend_additive);
    glUniform4f(ctx->height_fog.profile_loc,ctx->height_fog.density,
                ctx->height_fog.base_height,ctx->height_fog.falloff,
                ctx->height_fog.sun_strength);
}
static int gl33_set_height_fog(void* vctx, int on, float h, float k, float sun) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window || (on && (!ctx->hdr_on || ctx->hdr_finalized))) return 0;
    glfwMakeContextCurrent(ctx->window);
    ctx->height_fog.on = on;
    if (on) {
        ctx->height_fog.base_height = h;
        ctx->height_fog.falloff = k;
        ctx->height_fog.sun_strength = sun;
    }
    glUseProgram(ctx->program);
    gl33_fog_upload(ctx);
    return 1;
}
#endif
