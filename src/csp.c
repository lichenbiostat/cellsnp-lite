/* Utils
 * Author: Xianjie Huang <hxj5@hku.hk>
 */

#include <stdio.h>
#include <stdlib.h>
#include "htslib/sam.h"
#include "htslib/kstring.h"
#include "config.h"
#include "mplp.h"
#include "jfile.h"
#include "jstring.h"
#include "jsam.h"
#include "csp.h"

/*
 * Global settings
 */
void gll_setting_free(global_settings *gs) { 
    if (gs) {
        if (gs->in_fn_file) { free(gs->in_fn_file); gs->in_fn_file = NULL; }
        if (gs->in_fns) { str_arr_destroy(gs->in_fns, gs->nin); gs->in_fns = NULL; }
        if (gs->out_dir) { free(gs->out_dir); gs->out_dir = NULL; }
        if (gs->out_vcf_base) { jf_destroy(gs->out_vcf_base); gs->out_vcf_base = NULL; }
        if (gs->out_vcf_cells) { jf_destroy(gs->out_vcf_cells); gs->out_vcf_cells = NULL; } 
        if (gs->out_samples) { jf_destroy(gs->out_samples); gs->out_samples = NULL; }    
        if (gs->out_mtx_ad) { jf_destroy(gs->out_mtx_ad); gs->out_mtx_ad = NULL; }
        if (gs->out_mtx_dp) { jf_destroy(gs->out_mtx_dp); gs->out_mtx_dp = NULL; }
        if (gs->out_mtx_oth) { jf_destroy(gs->out_mtx_oth); gs->out_mtx_oth = NULL; } 
        if (gs->snp_list_file) { free(gs->snp_list_file); gs->snp_list_file = NULL; }
        snplist_destroy(gs->pl);
        if (gs->targets) { regidx_destroy(gs->targets); gs->targets = NULL; }
        if (gs->barcode_file) { free(gs->barcode_file); gs->barcode_file = NULL; }
        if (gs->barcodes) { str_arr_destroy(gs->barcodes, gs->nbarcode); gs->barcodes = NULL; }
        if (gs->sid_list_file) { free(gs->sid_list_file); gs->sid_list_file = NULL; }
        if (gs->sample_ids) { str_arr_destroy(gs->sample_ids, gs->nsid); gs->sample_ids = NULL; }
        if (gs->chroms) { str_arr_destroy(gs->chroms, gs->nchrom); gs->chroms = NULL; }
        if (gs->cell_tag) { free(gs->cell_tag); gs->cell_tag = NULL; }
        if (gs->umi_tag) { free(gs->umi_tag); gs->umi_tag = NULL; }
        if (gs->tp) { thpool_destroy(gs->tp); gs->tp = NULL; }
    }
}

/* print global settings. */
void gll_setting_print(FILE *fp, global_settings *gs, char *prefix) {
    if (gs) {
        int i;
        fprintf(fp, "%snum of input files = %d\n", prefix, gs->nin);
        fprintf(fp, "%sout_dir = %s\n", prefix, gs->out_dir);
        fprintf(fp, "%sis_out_zip = %d, is_genotype = %d\n", prefix, gs->is_out_zip, gs->is_genotype);
        fprintf(fp, "%sis_target = %d, num_of_pos = %ld\n", prefix, gs->is_target, 
                      gs->is_target ? 
                        (gs->targets ? (long) regidx_nregs(gs->targets) : 0) :
                        (long) snplist_size(gs->pl));
        fprintf(fp, "%snum_of_barcodes = %d, num_of_samples = %d\n", prefix, gs->nbarcode, gs->nsid);
        fprintf(fp, "%s%d chroms: ", prefix, gs->nchrom);
        for (i = 0; i < gs->nchrom; i++) fprintf(fp, "%s ", gs->chroms[i]);
        fputc('\n', fp);
        fprintf(fp, "%scell-tag = %s, umi-tag = %s\n", prefix, gs->cell_tag, gs->umi_tag);
        fprintf(fp, "%snthreads = %d, tp_max_open = %d\n", prefix, gs->nthread, gs->tp_max_open);
        fprintf(fp, "%smthreads = %d, tp_errno = %d, tp_ntry = %d\n", prefix, gs->mthread, gs->tp_errno, gs->tp_ntry);
        fprintf(fp, "%smin_count = %d, min_maf = %.2f, double_gl = %d\n", prefix, gs->min_count, gs->min_maf, gs->double_gl);
        fprintf(fp, "%smin_len = %d, min_mapq = %d\n", prefix, gs->min_len, gs->min_mapq);
        //fprintf(fp, "%smax_flag = %d\n", prefix, gs->max_flag);
        fprintf(fp, "%srflag_filter = %d, rflag_require = %d\n", prefix, gs->rflag_filter, gs->rflag_require);
        fprintf(fp, "%splp_max_depth = %d, no_orphan = %d\n", prefix, gs->plp_max_depth, gs->no_orphan);
    }
}

/*
 * Mpileup processing
 */
int csp_mplp_prepare(csp_mplp_t *mplp, global_settings *gs) {
    char **sgnames;
    int i, nsg;
    csp_plp_t *plp;
    /* init HashMap, pool of ul, pool of uu for mplp. */
    mplp->hsg = map_sg_init();
    if (NULL == mplp->hsg) { fprintf(stderr, "[E::%s] could not init map_sg_t structure.\n", __func__); return -1; }
    if (use_umi(gs)) {
        #if DEVELOP
            mplp->pl = pool_ul_init();
            if (NULL == mplp->pl) { fprintf(stderr, "[E::%s] could not init pool_ul_t structure.\n", __func__); return -1; }
            mplp->pu = pool_uu_init();
            if (NULL == mplp->pu) { fprintf(stderr, "[E::%s] could not init pool_uu_t structure.\n", __func__); return -1; }
        #endif
        mplp->su = pool_ps_init();
        if (NULL == mplp->su) { fprintf(stderr, "[E::%s] could not init pool_su_t structure.\n", __func__); return -1; }
    }
    /* set sample names for sample groups. */
    if (use_barcodes(gs)) { sgnames = gs->barcodes; nsg = gs->nbarcode; }
    else if (use_sid(gs)) { sgnames = gs->sample_ids; nsg = gs->nsid; }
    else { fprintf(stderr, "[E::%s] failed to set sample names.\n", __func__); return -1; }  // should not come here!
    if (csp_mplp_set_sg(mplp, sgnames, nsg) < 0) { fprintf(stderr, "[E::%s] failed to set sample names.\n", __func__); return -1; }
    /* init plp for each sample group in mplp->hsg and init HashMap plp->hug for UMI grouping. */
    for (i = 0; i < nsg; i++) {
        if (NULL == (plp = map_sg_val(mplp->hsg, mplp->hsg_iter[i]))) { 
            if (NULL == (map_sg_val(mplp->hsg, mplp->hsg_iter[i]) = plp = csp_plp_init())) {
                fprintf(stderr, "[E::%s] failed to init csp_plp_t for sg HashMap of csp_mplp_t.\n", __func__);
                return -1;
            }
        }
        if (use_umi(gs)) {
            plp->hug = map_ug_init();
            if (NULL == plp->hug) { fprintf(stderr, "[E::%s] could not init map_ug_t structure.\n", __func__); return -1; }
        }
    }
    return 0;
}

/*@note 1. To speed up, the caller should guarantee that:
           a) the parameters are valid, i.e. mplp and gs must not be NULL. In fact, this function is supposed to be 
              called after csp_mplp_t is created and set names of sample-groups, so mplp, mplp->hsg could not be NULL.
           b) the csp_pileup_t must have passed the read filtering, refer to pileup_read_with_fetch() for details.
           c) each key (sample group name) in map_sg_t already has a valid, not NULL, value (csp_plp_t*);
              This usually can be done by calling csp_mplp_prepare().
        2. This function is expected to be used by Mode1 & Mode2 & Mode3.

@discuss  In current version, only the result (base and qual) of the first read in one UMI group will be used for mplp statistics.
          TODO: store results of all reads in one UMI group (maybe could do consistency correction in each UMI group) and then 
          do mplp statistics.
 */
int csp_mplp_push(csp_pileup_t *pileup, csp_mplp_t *mplp, int sid, global_settings *gs) {
    map_sg_iter k;
    map_ug_iter u;
    csp_plp_t *plp = NULL;
    char **s;
    int r, idx;
    /* Push one csp_pileup_t into csp_mplp_t.
    *  The pileup->cb, pileup->umi could not be NULL as the pileuped read has passed filtering.
    */
    if (use_barcodes(gs)) { 
        if ((k = map_sg_get(mplp->hsg, pileup->cb)) == map_sg_end(mplp->hsg)) { return 1; }
        plp = map_sg_val(mplp->hsg, k);
    } else if (use_sid(gs)) { 
        plp = map_sg_val(mplp->hsg, mplp->hsg_iter[sid]);
    } else { return -1; }  // should not come here!
    if (use_umi(gs)) {
        u = map_ug_get(plp->hug, pileup->umi);
        if (u == map_ug_end(plp->hug)) {
            s = pool_ps_get(mplp->su);
            *s = strdup(pileup->umi);
            u = map_ug_put(plp->hug, *s, &r);
            if (r < 0) { return -2; }
            /* An example for pushing base & qual into HashMap of umi group.
            list_uu_t *ul = pool_ul_get(mplp->pl);
            umi_unit_t *uu = pool_uu_get(mplp->pu);
            uu->base = pileup->base; uu->qual = pileup->qual;
            list_uu_push(ul, uu);
            map_ug_val(plp->hug, u) = ul;
             */
            idx = seq_nt16_idx2int(pileup->base);
            plp->bc[idx]++;
            list_qu_push(plp->qu[idx], pileup->qual);
        } // else: do nothing.
    } else {
        idx = seq_nt16_idx2int(pileup->base);
        plp->bc[idx]++;
        list_qu_push(plp->qu[idx], pileup->qual);
    }
    return 0;
}

/*@discuss  In current version, only the result (base and qual) of the first read in one UMI group will be used for mplp statistics.
            TODO: store results of all reads in one UMI group (maybe could do consistency correction in each UMI group) and then 
            do mplp statistics.
 */
int csp_mplp_stat(csp_mplp_t *mplp, global_settings *gs) {
    csp_plp_t *plp = NULL;
    int i, j, k;
    size_t l;
    for (i = 0; i < mplp->nsg; i++) {
        plp = map_sg_val(mplp->hsg, mplp->hsg_iter[i]);
        for (j = 0; j < 5; j++) { 
            plp->tc += plp->bc[j]; 
            mplp->bc[j] += plp->bc[j];
        }
    }
    for (i = 0; i < 5; i++) { mplp->tc += mplp->bc[i]; }
    if (mplp->tc < gs->min_count) { return 1; }
    infer_allele(mplp->bc, &mplp->inf_rid, &mplp->inf_aid);   // must be called after mplp->bc are completely calculated.
    if (mplp->bc[mplp->inf_aid] < mplp->tc * gs->min_maf) { return 1; }
    if (mplp->ref_idx < 0 || mplp->alt_idx < 0) {  // ref or alt is not valid. Refer to csp_mplp_t.
        mplp->ref_idx = mplp->inf_rid;
        mplp->alt_idx = mplp->inf_aid;
    }
    mplp->ad = mplp->bc[mplp->alt_idx]; mplp->dp = mplp->bc[mplp->ref_idx] + mplp->ad; mplp->oth = mplp->tc - mplp->dp;
    for (i = 0; i < mplp->nsg; i++) {
        plp = map_sg_val(mplp->hsg, mplp->hsg_iter[i]);
        plp->ad = plp->bc[mplp->alt_idx]; if (plp->ad) mplp->nr_ad++;
        plp->dp = plp->bc[mplp->ref_idx] + plp->ad; if (plp->dp) mplp->nr_dp++;
        plp->oth = plp->tc - plp->dp; if (plp->oth) mplp->nr_oth++;
        if (gs->is_genotype) {
            for (j = 0; j < 5; j++) {
                for (l = 0; l < list_qu_size(plp->qu[j]); l++) {
                    if (get_qual_vector(list_qu_A(plp->qu[j], l), 45, 0.25, mplp->qvec) < 0) { return -1; }
                    for (k = 0; k < 4; k++) plp->qmat[j][k] += mplp->qvec[k];
                }
            }
            if (qual_matrix_to_geno(plp->qmat, plp->bc, mplp->ref_idx, mplp->alt_idx, gs->double_gl, plp->gl, &plp->ngl) < 0) { return -1; }
        }
    }
    return 0;
}

/*
* BAM/SAM/CRAM File API
 */

inline csp_bam_fs* csp_bam_fs_init(void) { return (csp_bam_fs*) calloc(1, sizeof(csp_bam_fs)); }

inline void csp_bam_fs_destroy(csp_bam_fs* p) {
    if (p) {
        if (p->idx) { hts_idx_destroy(p->idx); }
        if (p->hdr) { sam_hdr_destroy(p->hdr); }
        if (p->fp)  { hts_close(p->fp); }
        free(p);
    }
}

/* 
* Thread API
*/

/*@note      The pointer returned successfully by thdata_init() should be freed
             by thdata_destroy() when no longer used.
 */
inline thread_data* thdata_init(void) { return (thread_data*) calloc(1, sizeof(thread_data)); }

inline void thdata_destroy(thread_data *p) { free(p); }

inline void thdata_print(FILE *fp, thread_data *p) {
    fprintf(fp, "\tm = %ld, n = %ld\n", p->m, p->n);
    fprintf(fp, "\ti = %d, ret = %d\n", p->i, p->ret);
}

/*
 * File Routine
 */
inline jfile_t* create_tmp_fs(jfile_t *fs, int idx, int is_zip, kstring_t *s) {
    jfile_t *t;
    if (NULL == (t = jf_init())) { return NULL; }
    ksprintf(s, "%s.%d", fs->fn, idx); 
    t->fn = strdup(ks_str(s)); t->fm = "wb"; t->is_zip = is_zip; t->is_tmp = 1;
    return t;
}

jfile_t** create_tmp_files(jfile_t *fs, int n, int is_zip) {
    kstring_t ks = KS_INITIALIZE, *s = &ks;
    jfile_t *t = NULL, **tfs = NULL;
    int i, j;
    if (NULL == (tfs = (jfile_t**) calloc(n, sizeof(jfile_t*)))) { goto fail; }
    for (i = 0; i < n; i++) {
        if (NULL == (t = create_tmp_fs(fs, i, is_zip, s))) { goto fail; }
        else { tfs[i] = t; ks_clear(s); }
    } ks_free(s);
    return tfs;
  fail:
    ks_free(s);
    if (tfs) {
        for (j = 0; j < i; j++) jf_destroy(tfs[j]);
        free(tfs);
    }
    return NULL; 
}

inline int destroy_tmp_files(jfile_t **fs, const int n) {
    int i, m;
    m = jf_remove_all(fs, n);
    for (i = 0; i < n; i++) { jf_destroy(fs[i]); }
    free(fs);
    return m;
}

int merge_mtx(jfile_t *out, jfile_t **in, const int n, size_t *ns, size_t *nr, int *ret) {
    size_t k = 1, m = 0;
    int i = 0;
    kstring_t in_ks = KS_INITIALIZE, *in_buf = &in_ks;
    *ret = -1;
    if (! jf_isopen(out) && jf_open(out, NULL) <= 0) { *ret = -2; goto fail; }
    for (; i < n; i++) {
        if (jf_open(in[i], "rb") <= 0) { *ret = -2; goto fail; }
        while (jf_getln(in[i], in_buf) >= 0) {
            if (0 == ks_len(in_buf)) {    // empty line, meaning ending of a SNP.
                k++;
            } else {
                jf_printf(out, "%ld\t%s\n", k, ks_str(in_buf));
                m++; ks_clear(in_buf);
            }
        }
        jf_close(in[i]);
    }
    ks_free(in_buf); in_buf = NULL;
    *ns = k - 1; *nr = m;
    *ret = 0; 
    return i;
  fail:
    if (in_buf) { ks_free(in_buf); }
    if (i < n && jf_isopen(in[i])) { jf_close(in[i]); }
    return i;
}

int merge_vcf(jfile_t *out, jfile_t **in, const int n, int *ret) {
#define TMP_BUFSIZE 1048576
    size_t lr, lw;
    char buf[TMP_BUFSIZE];
    int i = 0;
    *ret = -1;
    if (! jf_isopen(out) && jf_open(out, NULL) <= 0) { *ret = -2; goto fail; }
    for (; i < n; i++) {
        if (jf_open(in[i], "rb") <= 0) { *ret = -2; goto fail; }
        while ((lr = jf_read(in[i], buf, TMP_BUFSIZE)) > 0) {
            lw = jf_write(out, buf, lr);
            if (lw != lr) { *ret = -2; goto fail; }
        }
        jf_close(in[i]);
    }
    *ret = 0;
    return i;
  fail:
    if (i < n && jf_isopen(in[i])) { jf_close(in[i]); }
    return i;
#undef TMP_BUFSIZE
}

/*@note      1. When proc = 1, the origial outputed mtx file was not filled with stat info:
                (totol SNPs, total samples, total records),
                so use this function to fill and rewrite.
             2. @p fs is not open when this function is just called and will keep not open when this function ends.
 */
int rewrite_mtx(jfile_t *fs, size_t ns, int nsmp, size_t nr) {
#define TMP_BUFSIZE 1048576
    kstring_t ks = KS_INITIALIZE, *s = &ks;
    jfile_t *new = NULL;
    char buf[TMP_BUFSIZE];
    int r, ret = -1;
    size_t lr, lw;
    if (NULL == (new = create_tmp_fs(fs, 0, fs->is_zip, s))) { goto fail; }
    ks_clear(s);
    if (jf_open(fs, "rb") <= 0 || jf_open(new, "wb") <= 0) { goto fail; }
    while ((r = jf_getln(fs, s)) >= 0 && ks_len(s) && ks_str(s)[0] == '%') {
        jf_puts(ks_str(s), new); jf_putc('\n', new);
        ks_clear(s);
    }
    if (r < 0 || 0 == ks_len(s)) { // has no records. TODO: distinguish EOF and error when r = 1.
        if (nr != 0) { ret = 1; goto fail; }
    }
    jf_printf(new, "%ld\t%d\t%ld\n", ns, nsmp, nr);
    if (nr) { 
        jf_puts(ks_str(s), new); jf_putc('\n', new);
        ks_clear(s);
    }  
    while ((lr = jf_read(fs, buf, TMP_BUFSIZE)) > 0) {
        lw = jf_write(new, buf, lr);
        if (lw != lr) { goto fail; }
    }
    jf_close(fs); jf_close(new);
    jf_remove(fs);
    if (rename(new->fn, fs->fn) != 0) { goto fail; }
    jf_destroy(new); new = NULL;
    ks_free(s);
    return 0;
  fail:
    ks_free(s);
    if (jf_isopen(fs)) { jf_close(fs); }
    if (new) {
        if (jf_isopen(new)) { jf_close(new); }
    }
    return ret;
#undef TMP_BUFSIZE
}

