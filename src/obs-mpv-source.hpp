#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <obs-module.h>
#include <mpv/client.h>
#include <mpv/render.h>

class MpvControlDock;

class ObsMpvSource {
    friend class MpvControlDock;
public:
    ObsMpvSource(obs_source_t *source, obs_data_t *settings);
    ~ObsMpvSource();

    static const char *obs_get_name(void *);
    static void *obs_create(obs_data_t *settings, obs_source_t *source);
    static void obs_destroy(void *data);
    static obs_properties_t *obs_get_properties(void *data);
    static void obs_properties_update(void *data, obs_data_t *settings);
    static uint32_t obs_get_width(void *data);
    static uint32_t obs_get_height(void *data);
    static void obs_video_tick(void *data, float seconds);

    // OBS Media Callbacks
    static void obs_media_play_pause(void *data, bool pause);
    static void obs_media_stop(void *data);
    static int64_t obs_media_get_duration(void *data);
    static int64_t obs_media_get_time(void *data);
    static void obs_media_set_time(void *data, int64_t ms);
    static enum obs_media_state obs_media_get_state(void *data);

    // Control methods accessible from Dock via settings
    void set_volume(double vol);
    double get_volume();
    
    struct MpvTrack {
        int id;
        std::string name;
        bool selected;
    };

    // Internal getters for state
    double get_time_pos();
    double get_duration();
    bool is_playing();
    std::vector<MpvTrack> get_tracks(const char *type);

private:
    obs_source_t *m_source;
    mpv_handle *m_mpv;
    mpv_render_context *m_mpv_render_ctx;
    
    uint32_t m_width;
    uint32_t m_height;
    std::string m_current_file_path;
    
    std::vector<uint8_t> m_sw_buffer;
    
    std::atomic<bool> m_events_available;
    std::atomic<bool> m_redraw_needed;

    // Audio via FIFO
    std::string m_fifo_path;
    std::thread m_audio_thread;
    std::atomic<bool> m_stop_audio_thread;
    void audio_thread_func();

    void handle_mpv_events();
    
    static void on_mpv_wakeup(void *ctx);
    static void on_mpv_render_update(void *ctx);
    static void on_mpv_audio_playback(void *ctx, void *data, int samples);

    // Internal direct controls
    void play();
    void pause();
    void stop();
    void seek(double seconds);
};
