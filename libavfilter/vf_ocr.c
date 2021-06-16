/*
 * Copyright (c) 2015 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <tesseract/capi.h>

#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct OCRContext {
    const AVClass *class;

    char *datapath;
    char *language;
    char *whitelist;
    char *blacklist;
    int x, y, x_in, y_in;
    int w, h, w_in, h_in;

    TessBaseAPI *tess;
} OCRContext;

#define OFFSET(x) offsetof(OCRContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption ocr_options[] = {
    { "datapath",  "set datapath",            OFFSET(datapath),  AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS },
    { "language",  "set language",            OFFSET(language),  AV_OPT_TYPE_STRING, {.str="eng"}, 0, 0, FLAGS },
    { "whitelist", "set character whitelist", OFFSET(whitelist), AV_OPT_TYPE_STRING, {.str="0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.:;,-+_!?\"'[]{}()<>|/\\=*&%$#@!~ "}, 0, 0, FLAGS },
    { "blacklist", "set character blacklist", OFFSET(blacklist), AV_OPT_TYPE_STRING, {.str=""},    0, 0, FLAGS },
    { "x",         "top x of sub region",     OFFSET(x),         AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, FLAGS },
    { "y",         "top y of sub region",     OFFSET(y),         AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, FLAGS },
    { "w",         "width of sub region",     OFFSET(w),         AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, FLAGS },
    { "h",         "height of sub region",    OFFSET(h),         AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, FLAGS },
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    OCRContext *s = ctx->priv;

    s->tess = TessBaseAPICreate();
    if (TessBaseAPIInit3(s->tess, s->datapath, s->language) == -1) {
        av_log(ctx, AV_LOG_ERROR, "failed to init tesseract\n");
        return AVERROR(EINVAL);
    }

    if (!TessBaseAPISetVariable(s->tess, "tessedit_char_whitelist", s->whitelist)) {
        av_log(ctx, AV_LOG_ERROR, "failed to set whitelist\n");
        return AVERROR(EINVAL);
    }

    if (!TessBaseAPISetVariable(s->tess, "tessedit_char_blacklist", s->blacklist)) {
        av_log(ctx, AV_LOG_ERROR, "failed to set blacklist\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG, "Tesseract version: %s\n", TessVersion());

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static void check_fix(int *x, int *y, int *w, int *h, int pic_w, int pic_h)
{
    // 0 <= x < pic_w
    if (*x >= pic_w)
        *x = 0;
    // 0 <= y < pic_h
    if (*y >= pic_h)
        *y = 0;

    if (*w == 0 || *w + *x > pic_w)
        *w = pic_w - *x;
    if (*h == 0 || *h + *y > pic_h)
        *h = pic_h - *y;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    OCRContext *s = ctx->priv;

    s->x_in = s->x;
    s->y_in = s->y;
    s->w_in = s->w;
    s->h_in = s->h;
    check_fix(&s->x_in, &s->y_in, &s->w_in, &s->h_in, inlink->w, inlink->h);
    if ( s->x_in != s->x || s->y_in != s->y  ||
        (s->w != 0 && s->w_in != s->w) || (s->h != 0 && s->h_in != s->h)) {
        av_log(s, AV_LOG_WARNING, "config error, subregion changed to "
                                  "x=%d, y=%d, w=%d, h=%d\n",
                                  s->x_in, s->y_in, s->w_in, s->h_in);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVDictionary **metadata = &in->metadata;
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    OCRContext *s = ctx->priv;
    char *result;
    int *confs;

    // TODO: support expression
    result = TessBaseAPIRect(s->tess, in->data[0], 1,
                             in->linesize[0], s->x_in, s->y_in, s->w_in, s->h_in);
    confs = TessBaseAPIAllWordConfidences(s->tess);
    av_dict_set(metadata, "lavfi.ocr.text", result, 0);
    for (int i = 0; confs[i] != -1; i++) {
        char number[256];

        snprintf(number, sizeof(number), "%d ", confs[i]);
        av_dict_set(metadata, "lavfi.ocr.confidence", number, AV_DICT_APPEND);
    }

    TessDeleteText(result);
    TessDeleteIntArray(confs);

    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    OCRContext *s = ctx->priv;

    TessBaseAPIEnd(s->tess);
    TessBaseAPIDelete(s->tess);
}

AVFILTER_DEFINE_CLASS(ocr);

static const AVFilterPad ocr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad ocr_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

const AVFilter ff_vf_ocr = {
    .name          = "ocr",
    .description   = NULL_IF_CONFIG_SMALL("Optical Character Recognition."),
    .priv_size     = sizeof(OCRContext),
    .priv_class    = &ocr_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = ocr_inputs,
    .outputs       = ocr_outputs,
};
