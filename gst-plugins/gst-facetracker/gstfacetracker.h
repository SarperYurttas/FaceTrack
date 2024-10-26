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

/**
 * @file GstFaceTracker.h
 * <b>NVIDIA DeepStream GStreamer Face Tracker API Specification </b>
 *
 * @b Description: This file specifies the data structures for
 * the DeepStream GStreamer Face Tracker Plugin.
 */

#ifndef __GST_FACETRACKER_H__
#define __GST_FACETRACKER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

#include "nvbufsurface.h"
#include "nvbufsurftransform.h"

G_BEGIN_DECLS

/**
 * @addtogroup three Standard GStreamer boilerplate
 * @{
 */
#define GST_TYPE_FACETRACKER \
    (gst_facetracker_get_type())
#define GST_FACETRACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FACETRACKER, GstFaceTracker))
#define GST_FACETRACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FACETRACKER, GstFaceTrackerClass))
#define GST_IS_FACETRACKER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FACETRACKER))
#define GST_IS_FACETRACKER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FACETRACKER))

typedef struct _GstFaceTracker GstFaceTracker;
typedef struct _GstFaceTrackerClass GstFaceTrackerClass;
/** @} */

/**
 * GstFaceTracker element structure.
 */
struct _GstFaceTracker
{
    GstBaseTransform element; /**< Should be the first member when extending from GstBaseTransform. */

    GstCaps *sinkcaps; /**< Sink pad caps */
    GstCaps *srccaps;  /**< Source pad caps */

    guint input_width;   /**<Input frame width */
    guint input_height;  /**<Input frame height */
    guint output_width;  /**<Output frame width */
    guint output_height; /**<Output frame height */

    GstBufferPool *pool; /**< Internal buffer pool for output buffers  */

    /** Input memory feature can take values MEM_FEATURE_NVMM/MEM_FEATURE_RAW
     * based on input  memory type caps*/
    gint input_feature;
    /** Output memory feature can take values MEM_FEATURE_NVMM/MEM_FEATURE_RAW
     * based on output  memory type caps*/
    gint output_feature;

    GstVideoFormat input_fmt;  /**< Input stream format derived from sink caps */
    GstVideoFormat output_fmt; /**< Output stream format derived from src caps */

    guint frame_num; /**< Number of the frame in the stream that was last processed. */

    gchar *output_resolution_str; /**< String contaning output resolution */

    NvBufSurfTransformRect prev_rect; /**< Keeps the information of rectangle in the previous frame */

    guint left_upper_limit; /**< Upper limit for the rectangle left */
    guint top_upper_limit;  /**< Upper limit for the rectangle top */
};

/** GStreamer boilerplate. */
struct _GstFaceTrackerClass
{
    GstBaseTransformClass parent_class;
};

GType gst_facetracker_get_type(void);

G_END_DECLS

#endif /* __GST_FACETRACKER_H__ */
