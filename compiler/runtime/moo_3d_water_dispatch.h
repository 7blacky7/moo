/* Included after dispatcher globals and Color3 parser. Every fetched value is owned. */
static MooValue moo_water_field(MooValue dict, const char* name) {
    MooValue key = moo_string_new(name);
    /* moo_dict_get consumes the lookup key and returns an owning value. */
    return moo_dict_get(dict, key);
}
static int moo_water_color(MooValue value, float out[3]) {
    if (value.tag == MOO_NUMBER) {
        double n = MV_NUM(value);
        if (!isfinite(n) || n < 0 || n > 16777215 || floor(n) != n) return 0;
    } else if (value.tag == MOO_STRING && MV_STR(value)) {
        const char* p = MV_STR(value)->chars;
        if (!p || strlen(p) != 7 || p[0] != '#') return 0;
        for (int i=1;i<7;++i)
            if (!((p[i]>='0' && p[i]<='9') || (p[i]>='a' && p[i]<='f') || (p[i]>='A' && p[i]<='F'))) return 0;
    } else return 0;
    Color3 c = parse_color3(value);
    float rgb[3] = {c.r,c.g,c.b};
    for(int i=0;i<3;++i)
        out[i] = rgb[i] <= 0.04045f ? rgb[i]/12.92f : powf((rgb[i]+0.055f)/1.055f,2.4f);
    return 1;
}
MooValue moo_3d_water_material(MooValue win, MooValue parameters) {
    if (!moo_3d_active_window(win) || !g_backend->set_water) {
        moo_throw(moo_string_new("water_material: aktives Wasser-faehiges HDR-Fenster erforderlich"));
        return moo_none();
    }
    if (parameters.tag != MOO_DICT || !MV_DICT(parameters)) {
        moo_throw(moo_string_new("water_material: Parameter-Dict erforderlich"));
        return moo_none();
    }
    const char* keys[7]={"ior","absorption","tint","sky_horizon","sky_zenith","max_distance","enabled"};
    MooValue v[6];
    for(int i=0;i<6;++i) v[i]=moo_water_field(parameters,keys[i]);
    MooWaterSettings settings = {0};
    int ok = v[0].tag==MOO_NUMBER && isfinite(MV_NUM(v[0])) && MV_NUM(v[0])>1 && MV_NUM(v[0])<=3;
    ok = ok && v[5].tag==MOO_NUMBER && isfinite(MV_NUM(v[5])) && MV_NUM(v[5])>=0 && MV_NUM(v[5])<=1000000;
    ok = ok && v[1].tag==MOO_LIST && MV_LIST(v[1]) && MV_LIST(v[1])->length==3;
    if (ok) {
        settings.ior=(float)MV_NUM(v[0]); settings.max_distance=(float)MV_NUM(v[5]);
        for(int i=0;i<3;++i) {
            MooValue a=MV_LIST(v[1])->items[i];
            if(a.tag!=MOO_NUMBER || !isfinite(MV_NUM(a)) || MV_NUM(a)<0 || MV_NUM(a)>1000000) { ok=0; break; }
            settings.absorption[i]=(float)MV_NUM(a);
        }
    }
    ok = ok && moo_water_color(v[2],settings.tint) && moo_water_color(v[3],settings.sky_horizon) && moo_water_color(v[4],settings.sky_zenith);
    for(int i=0;i<6;++i) moo_release(v[i]);
    if (!ok) {
        moo_throw(moo_string_new("water_material: finite ior (1,3], absorption RGB >=0, Farben und max_distance [0,1e6] erforderlich"));
        return moo_none();
    }
    if (!g_backend->set_water(g_ctx,&settings))
        moo_throw(moo_string_new("water_material: HDR-Frame mit opaque Hintergrund vor SceneEnd erforderlich"));
    return moo_none();
}
MooValue moo_3d_water_background(MooValue win) {
    if (!moo_3d_active_window(win) || !g_backend->water_background ||
        !g_backend->water_background(g_ctx)) {
        moo_throw(moo_string_new("water_background: aktiver HDR-Frame vor Wasser/SceneEnd erforderlich"));
    }
    return moo_none();
}
MooValue moo_3d_water_off(MooValue win) {
    if (!moo_3d_active_window(win) || !g_backend->set_water) {
        moo_throw(moo_string_new("water_off: aktives Wasser-faehiges Fenster erforderlich"));
        return moo_none();
    }
    g_backend->set_water(g_ctx,NULL);
    return moo_none();
}
