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
    static void obs_save(void *data, obs_data_t *settings);
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

    struct MpvTrack {
        int id;
        std::string name;
        bool selected;
    };

    struct PlaylistItem {
        std::string path;
        std::string name;
        double duration = 0.0;
        
        // Per-item settings
        double volume = 100.0;
        int audio_track = 1; // Default auto/first
        int sub_track = -1;  // Default off
        std::string ext_sub_path;
        
        bool loop = false;
        bool fade_in_enabled = false;
        double fade_in = 0.0;
        bool fade_out_enabled = false;
        double fade_out = 0.0;
        
        // Metadata
        std::vector<MpvTrack> audio_tracks;
        std::vector<MpvTrack> sub_tracks;
        double fps = 0.0;
        int audio_channels = 0;
        
        // Saved state
        double last_seek_pos = 0.0;
    };

    // Control methods accessible from Dock via settings
    void set_volume(double vol);
    double get_volume();
    
    // Internal getters for state
    double get_time_pos();
    double get_duration();
    bool is_playing();
    bool is_paused();
    bool is_idle();
    std::vector<MpvTrack> get_tracks(const char *type);
    
    // Playlist Management
    void playlist_add(const std::string& path);
    void playlist_add_multiple(const std::vector<std::string>& paths);
    void playlist_remove(int index);
    void playlist_move(int from, int to);
    void playlist_play(int index);
    void playlist_next();
    PlaylistItem* playlist_get_item(int index);
    int playlist_count();
    
    void set_auto_obs_fps(bool enabled);
    bool get_auto_obs_fps();

    void save_playlist(obs_data_t *settings);
    void load_playlist(obs_data_t *settings);
    
    // Metadata Probing
    struct FileMetadata {
        double duration;
        double fps = 0.0;
        int64_t channels = 0;
        std::vector<MpvTrack> audio_tracks;
        std::vector<MpvTrack> sub_tracks;
    };
    FileMetadata probe_file(const std::string& path);

private:
    obs_source_t *m_source;
    mpv_handle *m_mpv;
    mpv_render_context *m_mpv_render_ctx;
    
    std::vector<PlaylistItem> m_playlist;
    int m_current_index = -1;
    
    uint32_t m_width;
    uint32_t m_height;
    std::string m_current_file_path;
    
    std::vector<uint8_t> m_sw_buffer;
    
    std::deque<uint8_t> m_audio_queue;
    std::mutex m_audio_mutex;
    
    std::atomic<bool> m_events_available;
    std::atomic<bool> m_redraw_needed;
    std::atomic<bool> m_is_loading;
    bool m_auto_obs_fps = false;

    // Audio via FIFO
    std::string m_fifo_path;
#ifdef _WIN32
    void *m_pipe_handle; // HANDLE is void*
#endif
	std::atomic<bool> m_stop_audio_thread;
	std::atomic<bool> m_flush_audio_buffer;
	std::atomic<bool> m_av_sync_started;
	std::atomic<uint32_t> m_sample_rate;
	std::atomic<int> m_channels;
	std::thread m_audio_thread;
    void audio_thread_func();
    
    // A/V Sync
    std::atomic<uint64_t> m_total_audio_frames;
    std::atomic<uint64_t> m_audio_start_ts;

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
