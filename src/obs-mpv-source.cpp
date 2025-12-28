#include "obs-mpv-source.hpp"
#include <util/platform.h>
#include <util/threading.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <plugin-support.h>
#include <cinttypes>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define S_FILE_PATH "file_path"

struct obs_source_info mpv_source_info = {
	.id = "mpv_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_CONTROLLABLE_MEDIA,
	.get_name = ObsMpvSource::obs_get_name,
	.create = ObsMpvSource::obs_create,
	.destroy = ObsMpvSource::obs_destroy,
	.get_width = ObsMpvSource::obs_get_width,
	.get_height = ObsMpvSource::obs_get_height,
	.get_properties = ObsMpvSource::obs_get_properties,
	.update = ObsMpvSource::obs_properties_update,
	.video_tick = ObsMpvSource::obs_video_tick,
	.save = ObsMpvSource::obs_save,
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
	return props;
}

void ObsMpvSource::obs_properties_update(void *data, obs_data_t *settings) {
	auto self = static_cast<ObsMpvSource *>(data);
	if (!self->m_mpv || self->m_current_index < 0 || (size_t)self->m_current_index >= self->m_playlist.size()) return;

	auto& item = self->m_playlist[self->m_current_index];

	// Save settings to the playlist item
	item.audio_track = (int)obs_data_get_int(settings, "audio_track");
	item.sub_track = (int)obs_data_get_int(settings, "subtitle_track");
	item.loop = (int)obs_data_get_int(settings, "loop") != 0;
	item.volume = obs_data_get_double(settings, "volume");

	// Apply settings immediately if playing
	if (item.audio_track < 0) mpv_set_property_string(self->m_mpv, "aid", "no");
	else mpv_set_property_string(self->m_mpv, "aid", std::to_string(item.audio_track).c_str());

	if (item.sub_track < 0) mpv_set_property_string(self->m_mpv, "sid", "no");
	else mpv_set_property_string(self->m_mpv, "sid", std::to_string(item.sub_track).c_str());

	mpv_set_property_string(self->m_mpv, "loop-file", item.loop ? "inf" : "no");
	self->set_volume(item.volume);

	double sub_delay = obs_data_get_double(settings, "sub_delay");
	mpv_set_property(self->m_mpv, "sub-delay", MPV_FORMAT_DOUBLE, &sub_delay);

	double sub_scale = obs_data_get_double(settings, "sub_scale");
	mpv_set_property(self->m_mpv, "sub-scale", MPV_FORMAT_DOUBLE, &sub_scale);

	double sub_pos = obs_data_get_double(settings, "sub_pos");
	mpv_set_property(self->m_mpv, "sub-pos", MPV_FORMAT_DOUBLE, &sub_pos);

	const char *sub_path = obs_data_get_string(settings, "load_subtitle");
	if (sub_path && *sub_path) {
		item.ext_sub_path = sub_path;
		const char *cmd[] = {"sub-add", sub_path, "select", nullptr};
		mpv_command_async(self->m_mpv, 0, cmd);
		obs_data_set_string(settings, "load_subtitle", "");
	}
}

void ObsMpvSource::on_mpv_render_update(void *ctx) { static_cast<ObsMpvSource*>(ctx)->m_redraw_needed = true; }
void ObsMpvSource::on_mpv_wakeup(void *ctx) { static_cast<ObsMpvSource*>(ctx)->m_events_available = true; }
void ObsMpvSource::on_mpv_audio_playback(void *, void *, int) {}

void ObsMpvSource::obs_video_tick(void *data, float) {
	auto self = static_cast<ObsMpvSource *>(data);

	if (self->m_events_available) {
		self->m_events_available = false;
		self->handle_mpv_events();
	}

	if (!self->m_mpv_render_ctx) return;

	// Use verified simple video logic from 285e150
	if (mpv_render_context_update(self->m_mpv_render_ctx) & MPV_RENDER_UPDATE_FRAME || self->m_redraw_needed) {
		self->m_redraw_needed = false;

		int64_t w = 0, h = 0;
		mpv_get_property(self->m_mpv, "width", MPV_FORMAT_INT64, &w);
		mpv_get_property(self->m_mpv, "height", MPV_FORMAT_INT64, &h);

		if (w <= 0 || h <= 0) return;
		
		// Always update internal dims
		self->m_width = (uint32_t)w;
		self->m_height = (uint32_t)h;

		size_t stride = self->m_width * 4;
		if (self->m_sw_buffer.size() != stride * self->m_height) {
			self->m_sw_buffer.resize(stride * self->m_height);
		}

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

			if (!self->m_av_sync_started && !self->m_is_paused && !self->m_is_loading) {
				self->m_av_sync_started = true;
				self->m_audio_start_ts = frame.timestamp;
				self->m_total_audio_frames = 0;
			}

			obs_source_output_video(self->m_source, &frame);
		}
	}
}

ObsMpvSource::ObsMpvSource(obs_source_t *source, obs_data_t *settings) : m_source(source), m_width(0), m_height(0), m_events_available(false), m_redraw_needed(false), m_stop_audio_thread(false), m_flush_audio_buffer(false), m_av_sync_started(false), m_sample_rate(48000), m_channels(2), m_current_index(-1), m_is_loading(false), m_is_paused(false), m_in_transition(false), m_file_loaded_seen(false), m_total_audio_frames(0), m_audio_start_ts(0) {
	char fifo_path[256];
	#ifdef _WIN32
	snprintf(fifo_path, sizeof(fifo_path), "\\\\.\\pipe\\obs_mpv_audio_%p", this);
	m_fifo_path = fifo_path;
	m_pipe_handle = CreateNamedPipeA(m_fifo_path.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096 * 32, 4096 * 32, 0, NULL);
	#else
	snprintf(fifo_path, sizeof(fifo_path), "/tmp/obs_mpv_audio_%p", this);
	m_fifo_path = fifo_path;
	mkfifo(m_fifo_path.c_str(), 0666);
	#endif

	m_mpv = mpv_create();
	mpv_request_log_messages(m_mpv, "info");

	obs_audio_info oai;
	if (obs_get_audio_info(&oai)) {
		m_sample_rate = oai.samples_per_sec;
	}

	mpv_set_option_string(m_mpv, "vo", "libmpv");
	mpv_set_option_string(m_mpv, "hwdec", "videotoolbox-copy"); 
	mpv_set_option_string(m_mpv, "video-out-params/format", "bgra"); // Force BGRA for stability

	// Configure Audio to FIFO (RAW PCM) - FIXES from modern build
	mpv_set_option_string(m_mpv, "ao", "pcm");
	mpv_set_option_string(m_mpv, "ao-pcm-file", m_fifo_path.c_str());
	mpv_set_option_string(m_mpv, "ao-pcm-format", "float");
	mpv_set_option_string(m_mpv, "ao-pcm-waveheader", "no"); // Prevent pops
	
	mpv_set_option_string(m_mpv, "audio-format", "float");
	mpv_set_option_string(m_mpv, "keep-open", "yes");
	mpv_set_option_string(m_mpv, "keep-last-frame", "yes");

	mpv_initialize(m_mpv);
	mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);

	int adv = 1;
	mpv_render_param p[] = {{MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW}, {MPV_RENDER_PARAM_ADVANCED_CONTROL, &adv}, {MPV_RENDER_PARAM_INVALID, nullptr}};
	mpv_render_context_create(&m_mpv_render_ctx, m_mpv, p);

	mpv_set_wakeup_callback(m_mpv, on_mpv_wakeup, this);
	mpv_render_context_set_update_callback(m_mpv_render_ctx, on_mpv_render_update, this);

	m_auto_obs_fps = obs_data_get_bool(settings, "auto_obs_fps");
	load_playlist(settings);

	m_audio_thread = std::thread(&ObsMpvSource::audio_thread_func, this);
}

ObsMpvSource::~ObsMpvSource() {
	m_stop_audio_thread = true;
	if (m_audio_thread.joinable()) m_audio_thread.join();
	#ifdef _WIN32
	if (m_pipe_handle != (void*)-1) { DisconnectNamedPipe(m_pipe_handle); CloseHandle(m_pipe_handle); }
	#else
	unlink(m_fifo_path.c_str());
	#endif
	if (m_mpv_render_ctx) mpv_render_context_free(m_mpv_render_ctx);
	mpv_terminate_destroy(m_mpv);
}

void ObsMpvSource::audio_thread_func() {
	const size_t chunk_size = 4096;
	std::vector<uint8_t> buf(chunk_size);

#ifdef _WIN32
	if (m_pipe_handle == (void*)-1) return;
	BOOL connected = ConnectNamedPipe(m_pipe_handle, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
	while (!m_stop_audio_thread && connected) {
		DWORD bytes_read = 0;
		if (ReadFile(m_pipe_handle, buf.data(), (DWORD)chunk_size, &bytes_read, NULL) && bytes_read > 0) {
			std::lock_guard<std::mutex> lock(m_audio_mutex);
			m_audio_queue.insert(m_audio_queue.end(), buf.begin(), buf.begin() + bytes_read);
			size_t max_queue = m_sample_rate * m_channels * sizeof(float) * 2;
			if (m_audio_queue.size() > max_queue) { m_audio_queue.clear(); m_av_sync_started = false; }
		}
#else
	int fd = open(m_fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
	int retries = 50;
	while (fd < 0 && !m_stop_audio_thread && retries-- > 0) {
		fd = open(m_fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (fd < 0) return;
	while (!m_stop_audio_thread) {
		if (m_flush_audio_buffer) {
			while (read(fd, buf.data(), chunk_size) > 0);
			std::lock_guard<std::mutex> lock(m_audio_mutex);
			m_audio_queue.clear();
			m_flush_audio_buffer = false;
		}
		ssize_t bytes_read = read(fd, buf.data(), chunk_size);
		if (bytes_read > 0) {
			std::lock_guard<std::mutex> lock(m_audio_mutex);
			m_audio_queue.insert(m_audio_queue.end(), buf.begin(), buf.begin() + bytes_read);
			size_t max_queue = m_sample_rate * m_channels * sizeof(float) * 2;
			if (m_audio_queue.size() > max_queue) { m_audio_queue.clear(); m_av_sync_started = false; }
		}
#endif
		uint32_t rate = m_sample_rate;
		int chans = m_channels;
		if (rate > 0 && chans > 0 && !m_is_paused) {
			size_t frame_size = chans * sizeof(float);
			std::vector<uint8_t> out_buf;
			uint64_t ts = 0;
			uint32_t out_frames = 0;
			{
				std::lock_guard<std::mutex> lock(m_audio_mutex);
				if (m_audio_queue.size() >= frame_size) {
					uint64_t now = os_gettime_ns();
					if (!m_av_sync_started) { m_av_sync_started = true; m_audio_start_ts = now; m_total_audio_frames = 0; }
					uint64_t elapsed_ns = now - m_audio_start_ts;
					uint64_t target_frames = util_mul_div64(elapsed_ns, rate, 1000000000ULL) + (rate / 10);
					if (m_total_audio_frames < target_frames) {
						size_t available_frames = m_audio_queue.size() / frame_size;
						out_frames = (uint32_t)std::min((uint64_t)available_frames, target_frames - m_total_audio_frames);
						if (out_frames > 0) {
							size_t bytes = out_frames * frame_size;
							out_buf.assign(m_audio_queue.begin(), m_audio_queue.begin() + bytes);
							m_audio_queue.erase(m_audio_queue.begin(), m_audio_queue.begin() + bytes);
							ts = m_audio_start_ts + util_mul_div64(m_total_audio_frames, 1000000000ULL, rate);
							m_total_audio_frames += out_frames;
						}
					}
				}
			}
			if (out_frames > 0) {
				struct obs_source_audio audio = {};
				audio.samples_per_sec = rate;
				if (chans == 1) audio.speakers = SPEAKERS_MONO;
				else if (chans == 3) audio.speakers = SPEAKERS_2POINT1;
				else if (chans == 4) audio.speakers = SPEAKERS_4POINT0;
				else if (chans == 5) audio.speakers = SPEAKERS_4POINT1;
				else if (chans == 6) audio.speakers = SPEAKERS_5POINT1;
				else if (chans == 8) audio.speakers = SPEAKERS_7POINT1;
				else audio.speakers = SPEAKERS_STEREO;
				audio.format = AUDIO_FORMAT_FLOAT;
				audio.data[0] = out_buf.data();
				audio.frames = out_frames;
				audio.timestamp = ts;
				obs_source_output_audio(m_source, &audio);
			} else { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
		} else { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
	}
#ifndef _WIN32
	close(fd);
#endif
}

void ObsMpvSource::handle_mpv_events() {
	bool tracks_changed = false;
	while (m_mpv) {
		mpv_event *event = mpv_wait_event(m_mpv, 0);
		if (event->event_id == MPV_EVENT_NONE) break;
		if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
			auto msg = static_cast<mpv_event_log_message*>(event->data);
			blog(LOG_DEBUG, "[libmpv] %s: %s", msg->prefix, msg->text);
		}
		if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
			mpv_event_property *prop = (mpv_event_property *)event->data;
			if (strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
				m_is_paused = *(int *)prop->data != 0;
				if (m_is_paused) { std::lock_guard<std::mutex> lock(m_audio_mutex); m_audio_queue.clear(); }
			}
		}
		if (event->event_id == MPV_EVENT_AUDIO_RECONFIG) {
			int64_t new_rate = 0, new_chans = 0;
			mpv_get_property(m_mpv, "audio-params/samplerate", MPV_FORMAT_INT64, &new_rate);
			mpv_get_property(m_mpv, "audio-params/channel-count", MPV_FORMAT_INT64, &new_chans);
			if (new_rate > 0 && new_chans > 0) {
				if (m_sample_rate != (uint32_t)new_rate || m_channels != (int)new_chans) {
					if (m_av_sync_started) {
						uint64_t current_ts = m_audio_start_ts + util_mul_div64(m_total_audio_frames, 1000000000ULL, m_sample_rate);
						m_audio_start_ts = current_ts; m_total_audio_frames = 0;
					}
					m_sample_rate = (uint32_t)new_rate; m_channels = (int)new_chans; m_flush_audio_buffer = true;
				}
			}
		}
		if (event->event_id == MPV_EVENT_FILE_LOADED) {
			m_is_loading = false; tracks_changed = true;
		}
		if (event->event_id == MPV_EVENT_END_FILE) {
			auto end_ev = static_cast<mpv_event_end_file*>(event->data);
			if (end_ev->reason == MPV_END_FILE_REASON_EOF) playlist_next();
		}
	}
	if (tracks_changed) {
		obs_data_t *s = obs_source_get_settings(m_source);
		auto build_track_list = [&](const char *type) -> std::string {
			std::string out;
			auto tracks = get_tracks(type);
			for (const auto &t : tracks) {
				if (!out.empty()) out += "|";
				std::string name = t.name;
				if (strcmp(type, "audio") == 0 && t.channels > 0) {
					name += " (" + std::to_string(t.channels) + "ch)";
				}
				out += std::to_string(t.id) + ":" + name + ":" + (t.selected ? "1" : "0");
			}
			return out;
		};
		obs_data_set_string(s, "track_list_audio", build_track_list("audio").c_str());
		obs_data_set_string(s, "track_list_sub", build_track_list("sub").c_str());
		obs_data_release(s);
		obs_source_update(m_source, nullptr);
	}
}

void ObsMpvSource::playlist_add_multiple(const std::vector<std::string>& paths) {
	for (const auto& path : paths) {
		PlaylistItem item; item.path = path;
		size_t last_slash = path.find_last_of("/\\");
		item.name = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
		m_playlist.push_back(item);
	}
}

void ObsMpvSource::playlist_play(int index) {
	if (index >= 0 && (size_t)index < m_playlist.size()) {
		m_current_index = index; m_is_loading = true;
		auto& item = m_playlist[index];
		const char *cmd[] = {"loadfile", item.path.c_str(), nullptr};
		mpv_command_async(m_mpv, 0, cmd);
		mpv_set_property_string(m_mpv, "pause", "no");
	}
}

void ObsMpvSource::playlist_next() { if ((size_t)(m_current_index + 1) < m_playlist.size()) playlist_play(m_current_index + 1); }
void ObsMpvSource::playlist_add(const std::string& path) { playlist_add_multiple({path}); }
void ObsMpvSource::playlist_remove(int index) { if (index >= 0 && (size_t)index < m_playlist.size()) m_playlist.erase(m_playlist.begin() + index); }
void ObsMpvSource::playlist_move(int from, int to) { if (from >= 0 && to >= 0 && from < (int)m_playlist.size() && to < (int)m_playlist.size()) std::swap(m_playlist[from], m_playlist[to]); }
void ObsMpvSource::set_auto_obs_fps(bool e) { m_auto_obs_fps = e; }
bool ObsMpvSource::get_auto_obs_fps() { return m_auto_obs_fps; }
ObsMpvSource::PlaylistItem* ObsMpvSource::playlist_get_item(int i) { return (i>=0 && i<(int)m_playlist.size()) ? &m_playlist[i] : nullptr; }
int ObsMpvSource::playlist_count() { return (int)m_playlist.size(); }
void ObsMpvSource::play() { mpv_set_property_string(m_mpv, "pause", "no"); }
void ObsMpvSource::pause() { mpv_set_property_string(m_mpv, "pause", "yes"); }
void ObsMpvSource::stop() { mpv_command_string(m_mpv, "stop"); }
void ObsMpvSource::seek(double s) { mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &s); }
double ObsMpvSource::get_time_pos() { double v=0; mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &v); return v; }
double ObsMpvSource::get_duration() { double v=0; mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &v); return v; }
double ObsMpvSource::get_volume() { double v=100; mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &v); return v; }
void ObsMpvSource::set_volume(double vol) { mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol); }
bool ObsMpvSource::is_playing() { int p=1; mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p); return p==0; }
bool ObsMpvSource::is_paused() { int p=0; mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p); return p==1; }
bool ObsMpvSource::is_idle() { int i=0; mpv_get_property(m_mpv, "idle-active", MPV_FORMAT_FLAG, &i); return i==1; }

std::vector<ObsMpvSource::MpvTrack> ObsMpvSource::get_tracks(const char *type) {
	std::vector<MpvTrack> res; mpv_node node;
	if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &node) == 0) {
		if (node.format == MPV_FORMAT_NODE_ARRAY) {
			for (int i = 0; i < node.u.list->num; i++) {
				mpv_node *tr = &node.u.list->values[i];
				const char *t=nullptr, *l=nullptr, *title=nullptr; int64_t id=-1, chans=0; bool s=false;
				for (int j=0; j<tr->u.list->num; j++) {
					const char *k = tr->u.list->keys[j]; mpv_node *v = &tr->u.list->values[j];
					if (!strcmp(k, "type")) t=v->u.string; else if (!strcmp(k, "id")) id=v->u.int64;
					else if (!strcmp(k, "lang")) l=v->u.string; else if (!strcmp(k, "selected")) s=v->u.flag;
					else if (!strcmp(k, "demux-channel-count")) chans=v->u.int64;
					else if (!strcmp(k, "title")) title=v->u.string;
				}
				if (t && !strcmp(t, type)) {
					std::string name = title ? title : (l ? l : std::to_string(id));
					res.push_back({(int)id, name, s, (int)chans});
				}
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
enum obs_media_state ObsMpvSource::obs_media_get_state(void *d) {
	auto s = static_cast<ObsMpvSource*>(d);
	if (s->is_playing()) return OBS_MEDIA_STATE_PLAYING;
	if (s->is_paused()) return OBS_MEDIA_STATE_PAUSED;
	return OBS_MEDIA_STATE_STOPPED;
}

uint32_t ObsMpvSource::obs_get_width(void *d) { return static_cast<ObsMpvSource*>(d)->m_width > 0 ? static_cast<ObsMpvSource*>(d)->m_width : 1920; }
uint32_t ObsMpvSource::obs_get_height(void *d) { return static_cast<ObsMpvSource*>(d)->m_height > 0 ? static_cast<ObsMpvSource*>(d)->m_height : 1080; }

void ObsMpvSource::obs_save(void *data, obs_data_t *settings) { static_cast<ObsMpvSource*>(data)->save_playlist(settings); }

void ObsMpvSource::save_playlist(obs_data_t *settings) {
	obs_data_array_t *array = obs_data_array_create();
	for (const auto& item : m_playlist) {
		obs_data_t *obj = obs_data_create();
		obs_data_set_string(obj, "path", item.path.c_str());
		obs_data_set_string(obj, "name", item.name.c_str());
		obs_data_array_push_back(array, obj);
		obs_data_release(obj);
	}
	obs_data_set_array(settings, "playlist", array);
	obs_data_array_release(array);
}

void ObsMpvSource::load_playlist(obs_data_t *settings) {
	obs_data_array_t *array = obs_data_get_array(settings, "playlist");
	if (!array) return;
	m_playlist.clear();
	size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *obj = obs_data_array_item(array, i);
		PlaylistItem item;
		item.path = obs_data_get_string(obj, "path");
		item.name = obs_data_get_string(obj, "name");
		m_playlist.push_back(item);
		obs_data_release(obj);
	}
	obs_data_array_release(array);
}
