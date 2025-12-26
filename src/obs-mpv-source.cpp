#include "obs-mpv-source.hpp"
#include <util/platform.h>
#include <util/threading.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

#define S_FILE_PATH "file_path"

extern "C" struct obs_source_info mpv_source_info = {
    .id = "mpv_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_CONTROLLABLE_MEDIA,
    .get_name = ObsMpvSource::obs_get_name,
    .create = ObsMpvSource::obs_create,
    .destroy = ObsMpvSource::obs_destroy,
    .get_properties = ObsMpvSource::obs_get_properties,
    .update = ObsMpvSource::obs_properties_update,
    .get_width = ObsMpvSource::obs_get_width,
    .get_height = ObsMpvSource::obs_get_height,
    .video_tick = ObsMpvSource::obs_video_tick,
    .media_play_pause = ObsMpvSource::obs_media_play_pause,
    .media_stop = ObsMpvSource::obs_media_stop,
    .media_get_duration = ObsMpvSource::obs_media_get_duration,
    .media_get_time = ObsMpvSource::obs_media_get_time,
    .media_set_time = ObsMpvSource::obs_media_set_time,
    .media_get_state = ObsMpvSource::obs_media_get_state,
};

const char *ObsMpvSource::obs_get_name(void*) { return "MPV Source"; }

void *ObsMpvSource::obs_create(obs_data_t *settings, obs_source_t *source) {
    return new ObsMpvSource(source, settings);
}

void ObsMpvSource::obs_destroy(void *data) { delete static_cast<ObsMpvSource *>(data); }

obs_properties_t *ObsMpvSource::obs_get_properties(void *) {
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_path(props, S_FILE_PATH, "File Path", OBS_PATH_FILE, nullptr, nullptr);
    return props;
}

void ObsMpvSource::obs_properties_update(void *data, obs_data_t *settings) {
    auto self = static_cast<ObsMpvSource *>(data);
    if (!self->m_mpv) return;

    const char *path = obs_data_get_string(settings, S_FILE_PATH);
    if (path && *path && self->m_current_file_path != path) {
        self->m_current_file_path = path;
        const char *cmd[] = {"loadfile", path, nullptr};
        mpv_command_async(self->m_mpv, 0, cmd);
    }
    
    // Always apply these settings from the dock, removing has_user_value check
    int aid = (int)obs_data_get_int(settings, "audio_track");
    if (aid < 0) mpv_set_property_string(self->m_mpv, "aid", "no");
    else mpv_set_property_string(self->m_mpv, "aid", std::to_string(aid).c_str());

    int sid = (int)obs_data_get_int(settings, "subtitle_track");
    if (sid < 0) mpv_set_property_string(self->m_mpv, "sid", "no");
    else mpv_set_property_string(self->m_mpv, "sid", std::to_string(sid).c_str());

    int loop = (int)obs_data_get_int(settings, "loop");
    if (loop < 0) mpv_set_property_string(self->m_mpv, "loop-file", "inf");
    else mpv_set_property_string(self->m_mpv, "loop-file", std::to_string(loop).c_str());
    
    double vol = obs_data_get_double(settings, "volume");
    self->set_volume(vol);

    double sub_delay = obs_data_get_double(settings, "sub_delay");
    mpv_set_property(self->m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &sub_delay);

    double sub_scale = obs_data_get_double(settings, "sub_scale");
    mpv_set_property(self->m_mpv, "sub-scale", MPV_FORMAT_DOUBLE, &sub_scale);
    
    double sub_pos = obs_data_get_double(settings, "sub_pos");
    mpv_set_property(self->m_mpv, "sub-pos", MPV_FORMAT_DOUBLE, &sub_pos);

    const char *sub_path = obs_data_get_string(settings, "load_subtitle");
    if (sub_path && *sub_path) {
        const char *cmd[] = {"sub-add", sub_path, "select", nullptr};
        mpv_command_async(self->m_mpv, 0, cmd);
        obs_data_set_string(settings, "load_subtitle", "");
    }
}

void ObsMpvSource::on_mpv_render_update(void *ctx) { static_cast<ObsMpvSource*>(ctx)->m_redraw_needed = true; }
void ObsMpvSource::on_mpv_wakeup(void *ctx) { static_cast<ObsMpvSource*>(ctx)->m_events_available = true; }
void ObsMpvSource::on_mpv_audio_playback(void *, void *, int) {
    // Unused in FIFO implementation
}

void ObsMpvSource::obs_video_tick(void *data, float) {
    auto self = static_cast<ObsMpvSource *>(data);

    if (self->m_events_available) { 
        self->m_events_available = false; 
        self->handle_mpv_events(); 
    }
    
    if (self->m_mpv_render_ctx) {
        if (mpv_render_context_update(self->m_mpv_render_ctx) & MPV_RENDER_UPDATE_FRAME || self->m_redraw_needed) {
            self->m_redraw_needed = false;
            if (self->m_width <= 0 || self->m_height <= 0) return;

            size_t stride = self->m_width * 4;
            self->m_sw_buffer.resize(stride * self->m_height);

            int size[] = {(int)self->m_width, (int)self->m_height};
            mpv_render_param p[] = {{MPV_RENDER_PARAM_SW_SIZE, size}, {MPV_RENDER_PARAM_SW_FORMAT, (void*)"bgra"}, {MPV_RENDER_PARAM_SW_STRIDE, &stride}, {MPV_RENDER_PARAM_SW_POINTER, self->m_sw_buffer.data()}, {MPV_RENDER_PARAM_INVALID, nullptr}};

            if (mpv_render_context_render(self->m_mpv_render_ctx, p) >= 0) {
                struct obs_source_frame frame = {};
                frame.data[0] = self->m_sw_buffer.data();
                frame.linesize[0] = (uint32_t)stride;
                frame.width = self->m_width;
                frame.height = self->m_height;
                frame.format = VIDEO_FORMAT_BGRA;
                frame.timestamp = os_gettime_ns();
                obs_source_output_video(self->m_source, &frame);
            }
        }
    }
}

ObsMpvSource::ObsMpvSource(obs_source_t *source, obs_data_t *settings) : m_source(source), m_width(0), m_height(0), m_events_available(false), m_redraw_needed(false), m_stop_audio_thread(false) {
    // Setup FIFO for audio
    char fifo_path[256];
    snprintf(fifo_path, sizeof(fifo_path), "/tmp/obs_mpv_audio_%p", this);
    m_fifo_path = fifo_path;
    mkfifo(m_fifo_path.c_str(), 0666);
    
    m_mpv = mpv_create();
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    
    // Configure Audio to FIFO
    mpv_set_option_string(m_mpv, "ao", "pcm");
    mpv_set_option_string(m_mpv, "ao-pcm-file", m_fifo_path.c_str());
    mpv_set_option_string(m_mpv, "audio-samplerate", "48000");
    mpv_set_option_string(m_mpv, "audio-channels", "stereo");
    mpv_set_option_string(m_mpv, "audio-format", "float");
    
    mpv_initialize(m_mpv);
    
    int adv = 1;
    mpv_render_param p[] = {{MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW}, {MPV_RENDER_PARAM_ADVANCED_CONTROL, &adv}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_create(&m_mpv_render_ctx, m_mpv, p);
    
    mpv_set_wakeup_callback(m_mpv, on_mpv_wakeup, this);
    mpv_render_context_set_update_callback(m_mpv_render_ctx, on_mpv_render_update, this);
    
    obs_properties_update(this, settings);
    
    m_audio_thread = std::thread(&ObsMpvSource::audio_thread_func, this);
}

ObsMpvSource::~ObsMpvSource() {
    m_stop_audio_thread = true;
    if (m_audio_thread.joinable()) m_audio_thread.join();
    unlink(m_fifo_path.c_str());

    if (m_mpv_render_ctx) mpv_render_context_free(m_mpv_render_ctx);
    mpv_terminate_destroy(m_mpv);
}

void ObsMpvSource::audio_thread_func() {
    int fd = open(m_fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return;

    const size_t chunk_size = 4096;
    std::vector<uint8_t> buf(chunk_size);

    while (!m_stop_audio_thread) {
        ssize_t bytes_read = read(fd, buf.data(), chunk_size);
        if (bytes_read > 0) {
            struct obs_source_audio audio = {};
            audio.samples_per_sec = 48000;
            audio.speakers = SPEAKERS_STEREO;
            audio.format = AUDIO_FORMAT_FLOAT;
            audio.data[0] = buf.data();
            audio.frames = (uint32_t)(bytes_read / sizeof(float) / 2);
            audio.timestamp = os_gettime_ns();
            obs_source_output_audio(m_source, &audio);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    close(fd);
}

void ObsMpvSource::handle_mpv_events() {
    bool tracks_changed = false;
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
            int64_t w, h;
            mpv_get_property(m_mpv, "width", MPV_FORMAT_INT64, &w);
            mpv_get_property(m_mpv, "height", MPV_FORMAT_INT64, &h);
            m_width = (uint32_t)w; m_height = (uint32_t)h;
        } else if (event->event_id == MPV_EVENT_AUDIO_RECONFIG) {
            // TODO: Not implemented yet
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED) tracks_changed = true;
    }

    if (tracks_changed) {
        obs_data_t *s = obs_source_get_settings(m_source);
        auto build_track_list = [&](const char *type) -> std::string {
            std::string out;
            auto tracks = get_tracks(type);
            for (const auto &t : tracks) {
                if (!out.empty()) out += "|";
                out += std::to_string(t.id) + ":" + t.name + ":" + (t.selected ? "1" : "0");
            }
            return out;
        };
        obs_data_set_string(s, "track_list_audio", build_track_list("audio").c_str());
        obs_data_set_string(s, "track_list_sub", build_track_list("sub").c_str());
        obs_data_release(s);
    }
}

void ObsMpvSource::play() { mpv_set_property_string(m_mpv, "pause", "no"); }
void ObsMpvSource::pause() { mpv_set_property_string(m_mpv, "pause", "yes"); }
void ObsMpvSource::stop() { mpv_set_property_string(m_mpv, "pause", "yes"); double z=0; mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &z); }
void ObsMpvSource::seek(double s) { mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &s); }
double ObsMpvSource::get_time_pos() { double v=0; mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &v); return v; }
double ObsMpvSource::get_duration() { double v=0; mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &v); return v; }
bool ObsMpvSource::is_playing() { int p=1; mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p); return p==0; }
double ObsMpvSource::get_volume() { double v=100; mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &v); return v; }
void ObsMpvSource::set_volume(double vol) { mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol); }

std::vector<ObsMpvSource::MpvTrack> ObsMpvSource::get_tracks(const char *type) {
    std::vector<MpvTrack> res;
    mpv_node node;
    if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) == 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY) {
            for (int i = 0; i < node.u.list->num; i++) {
                mpv_node *tr = &node.u.list->values[i];
                const char *t=nullptr, *l=nullptr; int64_t id=-1; bool s=false;
                for (int j=0; j<tr->u.list->num; j++) {
                    const char *k = tr->u.list->keys[j]; mpv_node *v = &tr->u.list->values[j];
                    if (!strcmp(k, "type")) t=v->u.string; else if (!strcmp(k, "id")) id=v->u.int64;
                    else if (!strcmp(k, "lang")) l=v->u.string; else if (!strcmp(k, "selected")) s=v->u.flag;
                }
                if (t && !strcmp(t, type)) res.push_back({(int)id, l ? l : std::to_string(id), s});
            }
        }
        mpv_free_node_contents(&node);
    }
    return res;
}

void ObsMpvSource::obs_media_play_pause(void *d, bool p) { if (p) static_cast<ObsMpvSource*>(d)->pause(); else static_cast<ObsMpvSource*>(d)->play(); }
void ObsMpvSource::obs_media_stop(void *d) { static_cast<ObsMpvSource*>(d)->stop(); }
int64_t ObsMpvSource::obs_media_get_duration(void *d) { return (int64_t)(static_cast<ObsMpvSource*>(d)->get_duration()*1000); }
int64_t ObsMpvSource::obs_media_get_time(void *d) { return (int64_t)(static_cast<ObsMpvSource*>(d)->get_time_pos()*1000); }
void ObsMpvSource::obs_media_set_time(void *d, int64_t ms) { static_cast<ObsMpvSource*>(d)->seek(ms/1000.0); }
enum obs_media_state ObsMpvSource::obs_media_get_state(void *d) { return static_cast<ObsMpvSource*>(d)->is_playing() ? OBS_MEDIA_STATE_PLAYING : OBS_MEDIA_STATE_PAUSED; }

uint32_t ObsMpvSource::obs_get_width(void *d) { return static_cast<ObsMpvSource*>(d)->m_width; }
uint32_t ObsMpvSource::obs_get_height(void *d) { return static_cast<ObsMpvSource*>(d)->m_height; }
