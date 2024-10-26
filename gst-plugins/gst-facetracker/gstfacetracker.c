/**
 * SPDX-FileCopyrightText: Copyright (c) 2019-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>

#include "gstnvdsbufferpool.h"
#include "gstfacetracker.h"

#include "gstnvdsmeta.h"

#define DEFAULT_MEM_TYPE NVBUF_MEM_DEFAULT
#define DEFAULT_GPU_ID 0
#define DEFAULT_OUTPUT_BUFFERS 6
#define DEFAULT_BATCH_SIZE 1

#define DEFAULT_OUTPUT_WIDTH 1920
#define DEFAULT_OUTPUT_HEIGHT 1080

#define DEFAULT_INPUT_WIDTH 3840
#define DEFAULT_INPUT_HEIGHT 2160

#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"

GST_DEBUG_CATEGORY_STATIC(gst_facetracker_debug);
#define GST_CAT_DEFAULT gst_facetracker_debug

#ifndef PACKAGE
#define PACKAGE "facetracker"
#endif

#define PACKAGE_DESCRIPTION "Gstreamer Face Tracker Plugin"
#define PACKAGE_LICENSE "Proprietary"
#define PACKAGE_NAME "GStreamer Face Tracker Plugin"
#define PACKAGE_URL "http://nvidia.com/"

enum
{
    MEM_FEATURE_NVMM,
    MEM_FEATURE_RAW
};

enum
{
    PROP_0,
    PROP_OUTPUT_RESOLUTION,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
/* Input capabilities. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
                                                                   GST_PAD_SINK,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_NVMM,
                                                                                                                     "{ "
                                                                                                                     "RGBA }")));

/* Output capabilities. */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
                                                                  GST_PAD_SRC,
                                                                  GST_PAD_ALWAYS,
                                                                  GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_NVMM,
                                                                                                                    "{ "
                                                                                                                    "RGBA }")));

#define gst_facetracker_parent_class parent_class
G_DEFINE_TYPE(GstFaceTracker, gst_facetracker, GST_TYPE_BASE_TRANSFORM);

static void gst_facetracker_set_property(GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec);

static void gst_facetracker_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec);

static gboolean
gst_facetracker_accept_caps(GstBaseTransform *btrans,
                            GstPadDirection direction, GstCaps *caps)
{
    gboolean ret = TRUE;
    GstFaceTracker *facetracker = NULL;
    GstCaps *allowed = NULL;

    facetracker = GST_FACETRACKER(btrans);

    GST_DEBUG_OBJECT(facetracker, "accept caps %" GST_PTR_FORMAT, caps);

    /* get all the formats we can handle on this pad */
    if (direction == GST_PAD_SINK)
        allowed = facetracker->sinkcaps;
    else
        allowed = facetracker->srccaps;

    if (!allowed)
    {
        GST_DEBUG_OBJECT(facetracker, "failed to get allowed caps");
        goto no_transform_possible;
    }

    GST_DEBUG_OBJECT(facetracker, "allowed caps %" GST_PTR_FORMAT, allowed);

    /* intersect with the requested format */
    ret = gst_caps_is_subset(caps, allowed);
    if (!ret)
    {
        goto no_transform_possible;
    }

done:
    return ret;

/* ERRORS */
no_transform_possible:
{
    GST_DEBUG_OBJECT(facetracker,
                     "could not transform %" GST_PTR_FORMAT " in anything we support", caps);
    ret = FALSE;
    goto done;
}
}

static GstCaps *
gst_facetracker_fixate_caps(GstBaseTransform *trans,
                            GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
    GstStructure *ins, *outs;
    const GValue *from_par, *to_par;
    const gchar *from_fmt = NULL, *to_fmt = NULL;
    GstFaceTracker *facetracker = GST_FACETRACKER(trans);

    guint out_width, out_height;

    othercaps = gst_caps_make_writable(othercaps);

    GST_DEBUG_OBJECT(trans, "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, othercaps, caps);

    ins = gst_caps_get_structure(caps, 0);
    outs = gst_caps_get_structure(othercaps, 0);

    out_width = facetracker->output_width;
    out_height = facetracker->output_height;

    gst_structure_remove_fields(outs, "width", "height", NULL);

    gst_structure_set(outs, "width", G_TYPE_INT, out_width,
                      "height", G_TYPE_INT, out_height, NULL);

    from_fmt = gst_structure_get_string(ins, "format");
    to_fmt = gst_structure_get_string(outs, "format");

    if (!to_fmt)
    {
        /* Output format not fixed */
        if (!gst_structure_fixate_field_string(outs, "format", from_fmt))
        {
            return NULL;
        }
    }

    from_par = gst_structure_get_value(ins, "pixel-aspect-ratio");
    to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");

    /* we have both PAR but they might not be fixated */
    if (from_par && to_par)
    {
        gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
        gint count = 0, w = 0, h = 0;
        guint num, den;

        /* from_par should be fixed */
        g_return_val_if_fail(gst_value_is_fixed(from_par), othercaps);

        from_par_n = gst_value_get_fraction_numerator(from_par);
        from_par_d = gst_value_get_fraction_denominator(from_par);

        /* fixate the out PAR */
        if (!gst_value_is_fixed(to_par))
        {
            GST_DEBUG_OBJECT(trans, "fixating to_par to %dx%d", from_par_n,
                             from_par_d);
            gst_structure_fixate_field_nearest_fraction(outs, "pixel-aspect-ratio",
                                                        from_par_n, from_par_d);
        }

        to_par_n = gst_value_get_fraction_numerator(to_par);
        to_par_d = gst_value_get_fraction_denominator(to_par);

        /* if both width and height are already fixed, we can't do anything
         * about it anymore */
        if (gst_structure_get_int(outs, "width", &w))
            ++count;
        if (gst_structure_get_int(outs, "height", &h))
            ++count;
        if (count == 2)
        {
            GST_DEBUG_OBJECT(trans, "dimensions already set to %dx%d, not fixating",
                             w, h);
            g_print("%s: line=%d ---- %s\n", GST_ELEMENT_NAME(trans), __LINE__,
                    gst_caps_to_string(othercaps));
            return othercaps;
        }

        gst_structure_get_int(ins, "width", &from_w);
        gst_structure_get_int(ins, "height", &from_h);

        if (!gst_video_calculate_display_ratio(&num, &den, from_w, from_h,
                                               from_par_n, from_par_d, to_par_n, to_par_d))
        {
            GST_ELEMENT_ERROR(trans, CORE, NEGOTIATION, (NULL),
                              ("Error calculating the output scaled size - integer overflow"));
            g_print("%s: line=%d ---- %s\n", GST_ELEMENT_NAME(trans), __LINE__,
                    gst_caps_to_string(othercaps));
            return othercaps;
        }

        GST_DEBUG_OBJECT(trans,
                         "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
                         from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
        GST_DEBUG_OBJECT(trans, "resulting output should respect ratio of %d/%d",
                         num, den);

        /* now find a width x height that respects this display ratio.
         * prefer those that have one of w/h the same as the incoming video
         * using wd / hd = num / den */

        /* if one of the output width or height is fixed, we work from there */
        if (h)
        {
            GST_DEBUG_OBJECT(trans, "height is fixed,scaling width");
            w = (guint)gst_util_uint64_scale_int(h, num, den);
        }
        else if (w)
        {
            GST_DEBUG_OBJECT(trans, "width is fixed, scaling height");
            h = (guint)gst_util_uint64_scale_int(w, den, num);
        }
        else
        {
            /* none of width or height is fixed, figure out both of them based only on
             * the input width and height */
            /* check hd / den is an integer scale factor, and scale wd with the PAR */
            if (from_h % den == 0)
            {
                GST_DEBUG_OBJECT(trans, "keeping video height");
                h = from_h;
                w = (guint)gst_util_uint64_scale_int(h, num, den);
            }
            else if (from_w % num == 0)
            {
                GST_DEBUG_OBJECT(trans, "keeping video width");
                w = from_w;
                h = (guint)gst_util_uint64_scale_int(w, den, num);
            }
            else
            {
                GST_DEBUG_OBJECT(trans, "approximating but keeping video height");
                h = from_h;
                w = (guint)gst_util_uint64_scale_int(h, num, den);
            }
        }
        GST_DEBUG_OBJECT(trans, "scaling to %dx%d", w, h);

        /* now fixate */
        gst_structure_fixate_field_nearest_int(outs, "width", w);
        gst_structure_fixate_field_nearest_int(outs, "height", h);
    }
    else
    {
        gint width, height;

        if (gst_structure_get_int(ins, "width", &width))
        {
            if (gst_structure_has_field(outs, "width"))
            {
                gst_structure_fixate_field_nearest_int(outs, "width", width);
            }
        }
        if (gst_structure_get_int(ins, "height", &height))
        {
            if (gst_structure_has_field(outs, "height"))
            {
                gst_structure_fixate_field_nearest_int(outs, "height", height);
            }
        }
    }

    GST_DEBUG_OBJECT(trans, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

    // g_print ("%s: line=%d ---- %s\n", GST_ELEMENT_NAME(trans), __LINE__, gst_caps_to_string(othercaps));
    return othercaps;
}

static GstCaps *
gst_facetracker_transform_caps(GstBaseTransform *btrans,
                               GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(btrans);
    GstCapsFeatures *feature = NULL;
    GstCaps *new_caps = NULL;

    if (direction == GST_PAD_SINK)
    {
        new_caps =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGBA",
                                "width", G_TYPE_INT, facetracker->output_width, "height", G_TYPE_INT,
                                facetracker->output_height, NULL);
        feature = gst_caps_features_new("memory:NVMM", NULL);
        gst_caps_set_features(new_caps, 0, feature);
    }
    if (direction == GST_PAD_SRC)
    {
        new_caps =
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGBA",
                                "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE,
                                1, G_MAXINT, NULL);
        feature = gst_caps_features_new("memory:NVMM", NULL);
        gst_caps_set_features(new_caps, 0, feature);
    }

    if (gst_caps_is_fixed(caps))
    {
        GstStructure *fs = gst_caps_get_structure(caps, 0);
        const GValue *fps_value;
        guint i, n = gst_caps_get_size(new_caps);

        fps_value = gst_structure_get_value(fs, "framerate");

        // We cannot change framerate
        for (i = 0; i < n; i++)
        {
            fs = gst_caps_get_structure(new_caps, i);
            gst_structure_set_value(fs, "framerate", fps_value);
        }
    }
    return new_caps;
}

static gboolean
gst_facetracker_set_caps(GstBaseTransform *trans, GstCaps *incaps,
                         GstCaps *outcaps)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(trans);
    GstCapsFeatures *ift = NULL;
    GstStructure *config = NULL;
    GstVideoInfo in_info, out_info;

    GST_DEBUG_OBJECT(facetracker, "set_caps");

    if (!gst_video_info_from_caps(&in_info, incaps))
    {
        GST_ERROR("invalid input caps");
        return FALSE;
    }
    // facetracker->input_width = GST_VIDEO_INFO_WIDTH(&in_info);
    // facetracker->input_height = GST_VIDEO_INFO_HEIGHT(&in_info);
    facetracker->input_fmt =
        GST_VIDEO_FORMAT_INFO_FORMAT(in_info.finfo);

    if (!gst_video_info_from_caps(&out_info, outcaps))
    {
        GST_ERROR("invalid output caps");
        return FALSE;
    }
    // facetracker->output_width = GST_VIDEO_INFO_WIDTH (&facetracker->out_info);
    // facetracker->output_height = GST_VIDEO_INFO_HEIGHT (&facetracker->out_info);
    facetracker->output_fmt =
        GST_VIDEO_FORMAT_INFO_FORMAT(out_info.finfo);

    ift = gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_NVMM, NULL);
    if (gst_caps_features_is_equal(gst_caps_get_features(outcaps, 0), ift))
    {
        facetracker->output_feature = MEM_FEATURE_NVMM;
    }
    else
    {
        facetracker->output_feature = MEM_FEATURE_RAW;
    }

    if (gst_caps_features_is_equal(gst_caps_get_features(incaps, 0), ift))
    {
        facetracker->input_feature = MEM_FEATURE_NVMM;
    }
    else
    {
        facetracker->input_feature = MEM_FEATURE_RAW;
    }
    gst_caps_features_free(ift);

    // Pool Creation
    {
        facetracker->pool = gst_nvds_buffer_pool_new();

        config = gst_buffer_pool_get_config(facetracker->pool);

        // g_print ("in videoconvert caps = %s\n", gst_caps_to_string (outcaps));
        gst_buffer_pool_config_set_params(config, outcaps, sizeof(NvBufSurface),
                                          DEFAULT_OUTPUT_BUFFERS, DEFAULT_OUTPUT_BUFFERS);

        gst_structure_set(config,
                          "memtype", G_TYPE_UINT, DEFAULT_MEM_TYPE,
                          "gpu-id", G_TYPE_UINT, DEFAULT_GPU_ID,
                          "batch-size", G_TYPE_UINT, DEFAULT_BATCH_SIZE, NULL);

        /* set config for the created buffer pool */
        if (!gst_buffer_pool_set_config(facetracker->pool, config))
        {
            GST_WARNING("bufferpool configuration failed");
            return FALSE;
        }

        gboolean is_active = gst_buffer_pool_set_active(facetracker->pool, TRUE);
        if (!is_active)
        {
            GST_WARNING(" Failed to allocate the buffers inside the output pool");
            return FALSE;
        }
    }

    gst_base_transform_set_passthrough(trans, FALSE);
    return TRUE;
}

static GstFlowReturn
gst_facetracker_transform(GstBaseTransform *btrans,
                          GstBuffer *inbuf, GstBuffer *outbuf)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(btrans);
    GstMapInfo inmap;
    GstMapInfo outmap;
    NvBufSurface *in_surface = NULL;
    NvBufSurface *out_surface = NULL;

    NvBufSurfTransformParams transform_params;
    NvBufSurfTransform_Error err;

    NvBufSurfTransformRect src_rect = {0};

    NvDsBatchMeta *batch_meta;

    NvDsObjectMeta *obj_meta = NULL;
    NvDsMetaList *l_frame = NULL;
    NvDsMetaList *l_obj = NULL;

    GST_DEBUG_OBJECT(facetracker, "%s : Frame=%d InBuf=%p OutBuf=%p\n",
                     __func__, facetracker->frame_num, inbuf, outbuf);

    if (!gst_buffer_map(inbuf, &inmap, GST_MAP_READ))
        goto invalid_inbuf;

    if (!gst_buffer_map(outbuf, &outmap, GST_MAP_WRITE))
        goto invalid_outbuf;

    GST_DEBUG_OBJECT(facetracker, "transform");
    if (facetracker->input_feature == MEM_FEATURE_NVMM)
    {
        in_surface = (NvBufSurface *)inmap.data;
    }

    if (facetracker->output_feature == MEM_FEATURE_NVMM)
    {
        out_surface = (NvBufSurface *)outmap.data;
    }

    batch_meta = gst_buffer_get_nvds_batch_meta(inbuf);

    /* set current rect to prev rect if no detection found */
    src_rect.left = facetracker->prev_rect.left;
    src_rect.top = facetracker->prev_rect.top;
    src_rect.width = facetracker->output_width;
    src_rect.height = facetracker->output_height;

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)
    {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)(l_frame->data);

        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL; l_obj = l_obj->next)
        {
            obj_meta = (NvDsObjectMeta *)(l_obj->data);

            gfloat obj_x_center = obj_meta->rect_params.left + (obj_meta->rect_params.width / 2);
            gfloat obj_y_center = obj_meta->rect_params.top + (obj_meta->rect_params.height / 2);

            /* src rect should be in range between 0 and input width - output width */
            src_rect.left = (guint)MIN(facetracker->left_upper_limit, MAX(0, (obj_x_center - (facetracker->output_width / 2))));
            src_rect.top = (guint)MIN(facetracker->top_upper_limit, MAX(0, (obj_y_center - (facetracker->output_height / 2))));
            src_rect.width = (guint)facetracker->output_width;
            src_rect.height = (guint)facetracker->output_height;
        }
    }

    facetracker->prev_rect = src_rect;

    /* Set the transform parameters */
    transform_params.src_rect = &src_rect;
    transform_params.transform_flag = NVBUFSURF_TRANSFORM_CROP_SRC;
    transform_params.transform_flip = NvBufSurfTransform_None;

    err = NvBufSurfTransform(in_surface, out_surface, &transform_params);
    if (err != NvBufSurfTransformError_Success)
    {
        g_printerr("NvBufSurfTransform Error: %d!!!\n", err);
    }

    GST_BUFFER_PTS(outbuf) = GST_BUFFER_PTS(inbuf);

    gst_buffer_unmap(inbuf, &inmap);
    gst_buffer_unmap(outbuf, &outmap);

    facetracker->frame_num++;
    if (!gst_buffer_copy_into(outbuf, inbuf,
                              (GstBufferCopyFlags)GST_BUFFER_COPY_METADATA, 0, -1))
    {
        GST_DEBUG_OBJECT(facetracker, "Buffer metadata copy failed \n");
    }
    return GST_FLOW_OK;

invalid_inbuf:
{
    GST_ERROR("input buffer mapinfo failed");
    return GST_FLOW_ERROR;
}

invalid_outbuf:
{
    GST_ERROR_OBJECT(facetracker, "output buffer mapinfo failed");
    gst_buffer_unmap(inbuf, &inmap);
    return GST_FLOW_ERROR;
}
}

static GstFlowReturn
gst_facetracker_prepare_output_buffer(GstBaseTransform *trans,
                                      GstBuffer *inbuf, GstBuffer **outbuf)
{
    GstBuffer *gstOutBuf = NULL;
    GstFlowReturn result = GST_FLOW_OK;
    GstFaceTracker *facetracker = GST_FACETRACKER(trans);

    result = gst_buffer_pool_acquire_buffer(facetracker->pool, &gstOutBuf, NULL);
    GST_DEBUG_OBJECT(facetracker, "%s : Frame=%d Gst-OutBuf=%p\n", __func__,
                     facetracker->frame_num, gstOutBuf);

    if (result != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(facetracker,
                         "gst_facetracker_prepare_output_buffer failed");
        return result;
    }

    *outbuf = gstOutBuf;
    return result;
}

static gboolean
gst_facetracker_start(GstBaseTransform *btrans)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(btrans);

    facetracker->frame_num = 0;

    facetracker->left_upper_limit = facetracker->input_width - facetracker->output_width;
    facetracker->top_upper_limit = facetracker->input_height - facetracker->output_height;

    return TRUE;
}

static gboolean
gst_facetracker_stop(GstBaseTransform *btrans)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(btrans);

    GST_INFO_OBJECT(facetracker, " %s\n", __func__);

    if (facetracker->pool)
    {
        gst_buffer_pool_set_active(facetracker->pool, FALSE);
        gst_object_unref(facetracker->pool);
        facetracker->pool = NULL;
    }

    GST_DEBUG_OBJECT(facetracker, "gst_facetracker_stop");

    return TRUE;
}

/* initialize the facetracker's class */
static void
gst_facetracker_class_init(GstFaceTrackerClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseTransformClass *gstbasetransform_class =
        (GstBaseTransformClass *)klass;

    gobject_class = (GObjectClass *)klass;
    gstelement_class = (GstElementClass *)klass;

    // Indicates we want to use DS buf api
    g_setenv("DS_NEW_BUFAPI", "1", TRUE);

    gobject_class->set_property = gst_facetracker_set_property;
    gobject_class->get_property = gst_facetracker_get_property;

    gstbasetransform_class->transform_caps =
        GST_DEBUG_FUNCPTR(gst_facetracker_transform_caps);
    gstbasetransform_class->fixate_caps =
        GST_DEBUG_FUNCPTR(gst_facetracker_fixate_caps);
    gstbasetransform_class->accept_caps =
        GST_DEBUG_FUNCPTR(gst_facetracker_accept_caps);
    gstbasetransform_class->set_caps =
        GST_DEBUG_FUNCPTR(gst_facetracker_set_caps);

    gstbasetransform_class->transform =
        GST_DEBUG_FUNCPTR(gst_facetracker_transform);
    gstbasetransform_class->prepare_output_buffer =
        GST_DEBUG_FUNCPTR(gst_facetracker_prepare_output_buffer);

    gstbasetransform_class->start = GST_DEBUG_FUNCPTR(gst_facetracker_start);
    gstbasetransform_class->stop = GST_DEBUG_FUNCPTR(gst_facetracker_stop);

    gstbasetransform_class->passthrough_on_same_caps = FALSE;

    g_object_class_install_property(gobject_class, PROP_OUTPUT_RESOLUTION,
                                    g_param_spec_string("output-resolution", "Output resolution",
                                                        "Output resolution",
                                                        NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_details_simple(gstelement_class,
                                         "facetracker",
                                         "facetracker",
                                         "Gstreamer Face Tracker Element",
                                         "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
                                         "@ https://devtalk.nvidia.com/default/board/209/");

    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&src_factory));
    gst_element_class_add_pad_template(gstelement_class,
                                       gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_facetracker_init(GstFaceTracker *facetracker)
{
    facetracker->sinkcaps = gst_static_pad_template_get_caps(&sink_factory);
    facetracker->srccaps = gst_static_pad_template_get_caps(&src_factory);

    facetracker->pool = NULL;

    facetracker->output_width = DEFAULT_OUTPUT_WIDTH;
    facetracker->output_height = DEFAULT_OUTPUT_HEIGHT;

    facetracker->input_width = DEFAULT_INPUT_WIDTH;
    facetracker->input_height = DEFAULT_INPUT_HEIGHT;
}

static void
gst_facetracker_set_property(GObject *object, guint prop_id,
                             const GValue *value, GParamSpec *pspec)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(object);

    switch (prop_id)
    {
    case PROP_OUTPUT_RESOLUTION:
        facetracker->output_resolution_str = (gchar *)g_value_dup_string(value);
        {
            gint result = sscanf(facetracker->output_resolution_str, "%ux%u", &facetracker->output_width, &facetracker->output_height);

            // Error checking
            if (result == 2)
            {
                // Success: do nothing
            }
            else
            {
                g_print("Error in parsing output resolution\n");
                abort();
            }
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_facetracker_get_property(GObject *object, guint prop_id,
                             GValue *value, GParamSpec *pspec)
{
    GstFaceTracker *facetracker = GST_FACETRACKER(object);

    switch (prop_id)
    {
    case PROP_OUTPUT_RESOLUTION:
        g_value_set_string(value, facetracker->output_resolution_str);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* GstElement vmethod implementations */

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
facetracker_init(GstPlugin *facetracker)
{
    /* debug category for fltering log messages
     *
     * exchange the string 'Template facetracker' with your description
     */
    GST_DEBUG_CATEGORY_INIT(gst_facetracker_debug, "facetracker", 0, "facetracker");

    return gst_element_register(facetracker, "facetracker", GST_RANK_NONE,
                                GST_TYPE_FACETRACKER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  facetracker,
                  PACKAGE_DESCRIPTION,
                  facetracker_init, DS_VERSION, PACKAGE_LICENSE, PACKAGE_NAME, PACKAGE_URL)
