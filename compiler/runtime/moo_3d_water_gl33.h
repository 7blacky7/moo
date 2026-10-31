/* Included after GL33Context. The opaque snapshot is NEVER the draw attachment. */
static void gl33_water_destroy(GL33Context* ctx) {
    if (ctx->water.fbo) glDeleteFramebuffers(1, &ctx->water.fbo);
    if (ctx->water.color) glDeleteTextures(1, &ctx->water.color);
    if (ctx->water.depth) glDeleteTextures(1, &ctx->water.depth);
    ctx->water.fbo = ctx->water.color = ctx->water.depth = 0;
    ctx->water.width = ctx->water.height = ctx->water.valid = ctx->water.on = 0;
}
static int gl33_water_background(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->hdr_on || ctx->hdr_finalized || ctx->water.on ||
        !ctx->hdr_fbo || ctx->hdr_w <= 0 || ctx->hdr_h <= 0 ||
        (ctx->ao.on && !ctx->ao.opaque_done)) return 0;
    glfwMakeContextCurrent(ctx->window);
    GLint draw, read, active, texture;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    ctx->water.valid = 0;
    if (ctx->water.width != ctx->hdr_w || ctx->water.height != ctx->hdr_h) {
        gl33_water_destroy(ctx);
        glGenFramebuffers(1, &ctx->water.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->water.fbo);
        glGenTextures(1, &ctx->water.color);
        glBindTexture(GL_TEXTURE_2D, ctx->water.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ctx->hdr_w, ctx->hdr_h, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->water.color, 0);
        glGenTextures(1, &ctx->water.depth);
        glBindTexture(GL_TEXTURE_2D, ctx->water.depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, ctx->hdr_w, ctx->hdr_h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->water.depth, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
            glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
            gl33_water_destroy(ctx);
            return 0;
        }
        ctx->water.width = ctx->hdr_w;
        ctx->water.height = ctx->hdr_h;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->hdr_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->water.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, ctx->hdr_w, ctx->hdr_h, 0, 0, ctx->hdr_w, ctx->hdr_h,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glActiveTexture((GLenum)active);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    ctx->water.valid = 1;
    return 1;
}
static void gl33_water_upload(GL33Context* ctx) {
    if (!ctx->water.on) return;
    glUseProgram(ctx->program);
    glUniform1f(glGetUniformLocation(ctx->program, "uWaterOn"), 1.0f);
    glUniform2f(glGetUniformLocation(ctx->program, "uWaterSize"), (float)ctx->water.width, (float)ctx->water.height);
    glUniform2f(glGetUniformLocation(ctx->program, "uWaterProjection"), ctx->projection[10], ctx->projection[14]);
    glUniform3fv(glGetUniformLocation(ctx->program, "uWaterAbsorption"), 1, ctx->water.settings.absorption);
    glUniform3fv(glGetUniformLocation(ctx->program, "uWaterTint"), 1, ctx->water.settings.tint);
    glUniform3fv(glGetUniformLocation(ctx->program, "uWaterSkyHorizon"), 1, ctx->water.settings.sky_horizon);
    glUniform3fv(glGetUniformLocation(ctx->program, "uWaterSkyZenith"), 1, ctx->water.settings.sky_zenith);
    glUniform1f(glGetUniformLocation(ctx->program, "uWaterIor"), ctx->water.settings.ior);
    glUniform1f(glGetUniformLocation(ctx->program, "uWaterMaxDistance"), ctx->water.settings.max_distance);
    GLint active;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, ctx->water.color);
    glUniform1i(glGetUniformLocation(ctx->program, "uWaterColor"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, ctx->water.depth);
    glUniform1i(glGetUniformLocation(ctx->program, "uWaterDepth"), 5);
    glActiveTexture((GLenum)active);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
}
static int gl33_set_water(void* vctx, const MooWaterSettings* settings) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return 0;
    glfwMakeContextCurrent(ctx->window);
    if (settings) {
        if (!ctx->hdr_on || ctx->hdr_finalized || !ctx->water.valid) return 0;
        ctx->water.settings = *settings;
        ctx->water.on = 1;
        gl33_water_upload(ctx);
    } else {
        ctx->water.on = 0;
        glUseProgram(ctx->program);
        glUniform1f(glGetUniformLocation(ctx->program, "uWaterOn"), 0.0f);
        /* Normal renderer keeps blending enabled even at alpha 1; later alpha
         * setters only change the uniform/depth mask, not GL_BLEND itself. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, ctx->blend_additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask((ctx->alpha >= 0.999f && !ctx->blend_additive) ? GL_TRUE : GL_FALSE);
    }
    return 1;
}
