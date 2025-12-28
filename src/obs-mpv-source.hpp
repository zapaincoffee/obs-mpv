#pragma once

#include <obs-module.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

class ObsMpvSource {
public:
    struct MpvTrack {
        int id;
        std::string name;
        bool selected;
        int channels;
    };

    struct PlaylistItem {
        std::string path;
        std::string name;
        double duration = 0;
        double fps = 0;
        int audio_channels = 0;
        std::vector<MpvTrack> audio_tracks;
        std::vector<MpvTrack> sub_tracks;
        
        // Persistent settings
        int audio_track = -1; // -1 = auto/default
        int sub_track = -1;   // -1 = none
        bool loop = false;
        double volume = 100.0;
        std::string ext_sub_path;
        
        bool fade_in_enabled = false;
        double fade_in = 0.0;
        bool fade_out_enabled = false;
        double fade_out = 0.0;

        double last_seek_pos = 0;
    };

    struct FileMetadata {
        double duration;
        double fps;
        int64_t channels;
        std::vector<MpvTrack> audio_tracks;
        std::vector<MpvTrack> sub_tracks;
    };

    ObsMpvSource(obs_source_t *source, obs_data_t *settings);
    ~ObsMpvSource();

    static const char *obs_get_name(void *type_data);
    static void *obs_create(obs_data_t *settings, obs_source_t *source);
    static void obs_destroy(void *data);
    static uint32_t obs_get_width(void *data);
    static uint32_t obs_get_height(void *data);
    static obs_properties_t *obs_get_properties(void *data);
    static void obs_properties_update(void *data, obs_data_t *settings);
    static void obs_video_tick(void *data, float seconds);
    static void obs_save(void *data, obs_data_t *settings);

    // Media Control API
    static void obs_media_play_pause(void *data, bool pause);
    static void obs_media_stop(void *data);
    static int64_t obs_media_get_duration(void *data);
    static int64_t obs_media_get_time(void *data);
    static void obs_media_set_time(void *data, int64_t ms);
    static enum obs_media_state obs_media_get_state(void *data);

    void playlist_add(const std::string& path);
    void playlist_add_multiple(const std::vector<std::string>& paths);
    void playlist_remove(int index);
    void playlist_move(int from, int to);
    void playlist_play(int index);
    void playlist_next();
    int playlist_count();
    PlaylistItem* playlist_get_item(int index);

    void set_auto_obs_fps(bool enabled);
    bool get_auto_obs_fps();

    void play();
    void pause();
    void stop();
    void seek(double seconds);
    double get_time_pos();
    double get_duration();
    double get_volume();
    void set_volume(double volume);
    bool is_playing();
    bool is_paused();
    bool is_idle();

    std::vector<MpvTrack> get_tracks(const char *type);

    std::vector<PlaylistItem> m_playlist;
    int m_current_index;

private:
    obs_source_t *m_source;
    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpv_render_ctx = nullptr;
    uint32_t m_width, m_height;
    std::atomic<bool> m_events_available;
    std::atomic<bool> m_redraw_needed;

    // Software rendering cache
    std::vector<uint8_t> m_sw_buffer;
    uint32_t m_sw_w = 0;
    uint32_t m_sw_h = 0;

    // Audio thread & FIFO
    std::thread m_audio_thread;
    std::atomic<bool> m_stop_audio_thread;
    std::atomic<bool> m_flush_audio_buffer;
    std::string m_fifo_path;
    std::vector<uint8_t> m_audio_queue;
    std::mutex m_audio_mutex;
    
    // Windows-specific pipe handle
#ifdef _WIN32
    void* m_pipe_handle = (void*)-1;
#endif

    // Sync helpers
    std::atomic<bool> m_av_sync_started;
    uint32_t m_sample_rate;
    int m_channels;
    uint64_t m_total_audio_frames;
    uint64_t m_audio_start_ts;

    std::atomic<bool> m_is_loading;
    std::atomic<bool> m_is_paused;
    std::atomic<bool> m_in_transition;
    std::atomic<bool> m_file_loaded_seen;
    bool m_auto_obs_fps = false;

    void handle_mpv_events();
    void audio_thread_func();
    void save_playlist(obs_data_t *settings);
    void load_playlist(obs_data_t *settings);

    static void on_mpv_wakeup(void *ctx);
    static void on_mpv_render_update(void *ctx);
    static void on_mpv_audio_playback(void *ctx, void *data, int n_samples);
};
