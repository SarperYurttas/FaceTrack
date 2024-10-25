#include <gst/gst.h>

#define CHECK_GST_SET_STATE(gst_state, error_str)    \
    do                                               \
    {                                                \
        if ((gst_state) == GST_STATE_CHANGE_FAILURE) \
        {                                            \
            g_print("Gst error: %s\n", error_str);   \
            g_abort();                               \
        }                                            \
    } while (0)

static GstElement *get_pipeline(const gchar *pipeline_str)
{
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(pipeline_str, &error);
    if (NULL == pipeline)
        g_printerr("Failed to parse launch: %s\n", error->message);
    if (error)
        g_error_free(error);
    return pipeline;
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *main_loop = (GMainLoop *)data;
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
        g_print("End of stream event received, exiting...\n");
        g_main_loop_quit(main_loop);
        break;
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *error;
        gst_message_parse_error(msg, &error, &debug);
        g_printerr("ERROR from element %s: %s\n",
                   GST_OBJECT_NAME(msg->src), error->message);
        if (debug)
            g_printerr("Error details: %s\n", debug);
        g_free(debug);
        g_error_free(error);
        g_main_loop_quit(main_loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

static GstElement *build_pipeline(gchar *input_location, gchar *output_location)
{
    gchar *pipeline_format_str = "uridecodebin uri=file://%s ! mux.sink_0 \
               nvstreammux name=mux batch-size=1 live-source=1 width=3840 height=2160 \
               ! queue ! nvinfer config-file-path=/workspace/config/config_infer_primary_facedetectir.txt \
               ! queue ! nvtracker ll-lib-file=/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so ll-config-file=/workspace/config/config_tracker_NvDCF_accuracy.yml tracker-width=1920 tracker-height=1088 \
               ! nvstreamdemux name=demux \
               demux.src_0 ! queue ! nvdsosd ! videoconvert ! nvv4l2h264enc ! h264parse ! qtmux ! filesink location=%s";
    gchar pipeline_str[4096];

    int n = snprintf(pipeline_str, sizeof(pipeline_str), pipeline_format_str, input_location, output_location);
    if (n >= sizeof(pipeline_str))
    {
        g_printerr("Pipeline string was truncated. Buffer size is insufficient.\n");
        return NULL;
    }
    GstElement *pipeline = get_pipeline(pipeline_str);
}

int main(int argc, char *argv[])
{
    gst_init(NULL, NULL);

    gchar *input_file = argv[1];
    gchar *output_file = argv[2];

    GstBus *bus = NULL;
    guint bus_watch_id;

    GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);

    GstElement *pipeline = build_pipeline(input_file, output_file);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, main_loop);
    gst_object_unref(bus);

    CHECK_GST_SET_STATE(gst_element_set_state(pipeline, GST_STATE_PLAYING), "Failed to set output pipeline to playing. Aborting.\n");

    /* Start the main loop */
    g_main_loop_run(main_loop);

    /* Out of the main loop, clean up nicely */
    g_print("Returned, stopping playback\n");

    /* Delete sources */
    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_print("Deleting pipeline\n");
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(main_loop);

    return 0;
}