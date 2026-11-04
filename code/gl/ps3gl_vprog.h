/* ps3gl_vprog.h -- ps3_vprog 1: per-draw q3_vp_gen params, shared by ps3gl and the renderer.
 * Plain C types only, so renderer TUs can include it next to tr_local.h. */
#ifndef PS3GL_VPROG_H
#define PS3GL_VPROG_H

/* q3_vp_gen.vcg's uniforms (same names, see there), plus the raw tess arrays behind ATTR2/3/8/9 */
typedef struct {
    float       cmul[4], cadd[4];   /* oColor = color * cmul + cadd */
    float       tm[2][16];          /* per bundle: s over (st, lm, xyz, 1), then t over the same */
    float       tsel[4];            /* env bundle 0 / 1, turb bundle 0 / 1 */
    float       turb[2][4], turbm[2][4];
    float       csel[4];            /* lightingDiffuse, lightingSpecular, portal, any */
    float       lamb[4], lambi[4], ldir[4], ldirc[4];
    float       vorg[4], porg[4];
    float       df[3][16];          /* deform slots: dfa, dfb, dfc, dfm */
    const void *color;              /* 4 x u8 per vertex, stride 4 */
    const void *st, *lm;            /* 2 x f32 per vertex, stride tc_stride */
    const void *normal;             /* 3 x f32 per vertex, stride 16; NULL when q3_vp_gen reads no normal */
    int         tc_stride;
} ps3gl_vparams_t;

void ps3gl_SetVProg(int on);                        /* once per frame, from the ps3_vprog cvar */
int  ps3gl_VProgOn(void);                           /* ps3_vprog 1 and q3_vp_gen loaded */
void ps3gl_VertexParams(const ps3gl_vparams_t *p);  /* one-shot: applies to the next ps3gl_DrawElements */

#endif
