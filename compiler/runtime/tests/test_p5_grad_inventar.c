#include "../moo_distill.h"
#include "../moo_dpo.h"
#include "../moo_ki_gpu_api.h"
static int fehlerzahl=0;
#include <math.h>
#include <stdio.h>
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e) { moo_release(e); moo_error_flag = 1; }
void moo_socket_free(void *p) {(void)p;} void moo_thread_free(void *p) {(void)p;}
void moo_channel_free(void *p) {(void)p;} void moo_db_free(void *p) {(void)p;}
void moo_db_stmt_free(void *p) {(void)p;} void moo_window_free(void *p) {(void)p;}
void moo_web_free(void *p) {(void)p;} void moo_voxel_free(void *p) {(void)p;}
void moo_frame_free(void *p) {(void)p;} void moo_gif_handle_free(void *p) {(void)p;}
void moo_video_handle_free(void *p) {(void)p;}
static MooValue tensor(int ndim, int32_t *shape, const float *values) {
    MooTensor *t = moo_tensor_raw(ndim, shape);
    for (int64_t i = 0; i < t->size; ++i) t->data[i] = values[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t); return v;
}

static double eval_loss(MooValue layers, MooValue x, MooValue y, MooValue mask) {
    MooValue out=moo_nn_vorwaerts(layers,x);
    MooValue loss=moo_nn_kreuzentropie_maskiert(out,y,mask);
    moo_tensor_f32_sichern(MV_TENSOR(loss));
    double v=MV_TENSOR(loss)->data[0];
    moo_release(out); moo_release(loss); return v;
}
static void probe(const char *name, float *mask_values) {
    MooValue layers=moo_list_new(2);
    moo_list_append(layers,moo_nn_schicht_embedding(moo_number(4),moo_number(4),moo_number(17)));
    MooValue act=moo_string_new("keine");
    moo_list_append(layers,moo_nn_schicht_dicht(moo_number(4),moo_number(4),act,moo_number(19)));
    moo_release(act);
    MooValue params=moo_nn_parameter(layers);
    int32_t shape[1]={4}; float xv[]={0,1,2,0}, yv[]={1,2,3,0};
    MooValue x=tensor(1,shape,xv), y=tensor(1,shape,yv), mask=tensor(1,shape,mask_values);
    moo_ag_an();
    MooValue out=moo_nn_vorwaerts(layers,x), loss=moo_nn_kreuzentropie_maskiert(out,y,mask);
    moo_release(moo_tensor_rueckwaerts(loss));
    for (int n=0;n<MV_LIST(params)->length;n++) moo_tensor_grad_sichern(MV_TENSOR(MV_LIST(params)->items[n]));
    moo_release(out); moo_release(loss); moo_ag_reset(); moo_ag_aus();
    for (int n=0;n<MV_LIST(params)->length;n++) {
        MooTensor *p=MV_TENSOR(MV_LIST(params)->items[n]);
        moo_tensor_f32_sichern(p);
        int bad=0; double maxerr=0, worstfd=0, worstag=0; int worst=-1;
        for (int64_t i=0;i<p->size;i++) {
            float orig=p->data[i], grad=p->grad ? p->grad[i] : 0;
            p->data[i]=orig+0.001f; p->valid=MOO_V_DATA;
            double plus=eval_loss(layers,x,y,mask);
            p->data[i]=orig-0.001f; p->valid=MOO_V_DATA;
            double minus=eval_loss(layers,x,y,mask);
            p->data[i]=orig; p->valid=MOO_V_DATA;
            double fd=(plus-minus)/0.002, err=fabs(fd-grad);
            if (err>0.003) bad++;
            if (err>maxerr) {maxerr=err; worst=(int)i; worstfd=fd; worstag=grad;}
        }
        fehlerzahl += bad;
        printf("%s param=%d size=%lld bad=%d maxerr=%.7g index=%d autograd=%.7g fd=%.7g\n",name,n,(long long)p->size,bad,maxerr,worst,worstag,worstfd);
    }
    moo_release(x); moo_release(y); moo_release(mask); moo_release(params); moo_release(layers); moo_ag_reset();
}
int main(void) {
    float last[]={0,0,1,0}, first[]={1,0,0,0}, middle[]={0,1,0,0}, full[]={1,1,1,1};
    probe("einzel_pos2",last); probe("einzel_pos0",first); probe("einzel_pos1",middle); probe("voll",full);
    MooKiGpuTelemetrie tel; moo_ki_gpu_telemetrie(&tel);
    printf("GPU submits=%llu uploads=%llu downloads=%llu cpu_fallbacks=%llu\n", (unsigned long long)tel.submits, (unsigned long long)tel.uploads, (unsigned long long)tel.downloads, (unsigned long long)tel.cpu_fallbacks);
    return moo_error_flag || fehlerzahl ? 1 : 0;
}
