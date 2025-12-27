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
void ObsMpvSource::on_mpv_audio_playback(void *, void *, int) {
	// Unused in FIFO implementation
}

void ObsMpvSource::obs_video_tick(void *data, float) {
	auto self = static_cast<ObsMpvSource *>(data);

	if (self->m_events_available) {
		self->m_events_available = false;
		self->handle_mpv_events();
	}
	// if (self->m_is_loading) return; // Removed to prevent blackout during transitions

	if (self->m_mpv_render_ctx) {
		if (mpv_render_context_update(self->m_mpv_render_ctx) & MPV_RENDER_UPDATE_FRAME || self->m_redraw_needed) {
			self->m_redraw_needed = false;

			// Check dimensions before rendering to prevent flashing
			int64_t w = 0, h = 0;
			mpv_get_property(self->m_mpv, "width", MPV_FORMAT_INT64, &w);
			mpv_get_property(self->m_mpv, "height", MPV_FORMAT_INT64, &h);

			if (w <= 0 || h <= 0) return;
			if ((uint32_t)w != self->m_width || (uint32_t)h != self->m_height) {
				self->m_width = (uint32_t)w;
				self->m_height = (uint32_t)h;
				return; // Wait for next tick with correct size
			}

			size_t stride = self->m_width * 4;
			self->m_sw_buffer.resize(stride * self->m_height);

			double video_pts = -1.0;
			int size[] = {(int)self->m_width, (int)self->m_height};
			mpv_render_param p[] = {
				{MPV_RENDER_PARAM_SW_SIZE, size},
				{MPV_RENDER_PARAM_SW_FORMAT, (void*)"bgra"},
				{MPV_RENDER_PARAM_SW_STRIDE, &stride},
				{MPV_RENDER_PARAM_SW_POINTER, self->m_sw_buffer.data()},
				// Request the PTS of the rendered frame.
				{MPV_RENDER_PARAM_VIDEO_PTS, &video_pts},
				{MPV_RENDER_PARAM_INVALID, nullptr}
			};

			if (mpv_render_context_render(self->m_mpv_render_ctx, p) >= 0) {
				struct obs_source_frame frame = {};
				frame.data[0] = self->m_sw_buffer.data();
				frame.linesize[0] = (uint32_t)stride;
				frame.width = self->m_width;
				frame.height = self->m_height;
				frame.format = VIDEO_FORMAT_BGRA;
				
				if (video_pts > 0) {
					frame.timestamp = (uint64_t)(video_pts * 1000000000.0);
				} else {
					// Fallback if PTS is not available
					frame.timestamp = os_gettime_ns();
				}

				if (!self->m_av_sync_started && video_pts > 0) {
					self->m_av_sync_started = true;
					self->m_audio_start_ts = frame.timestamp;
					self->m_total_audio_frames = 0;
					blog(LOG_INFO, "A/V sync started at %" PRIu64, self->m_audio_start_ts);
				}

				obs_source_output_video(self->m_source, &frame);
			}
		}
	}
}

ObsMpvSource::ObsMpvSource(obs_source_t *source, obs_data_t *settings) : m_source(source), m_width(0), m_height(0), m_events_available(false), m_redraw_needed(false), m_stop_audio_thread(false), m_flush_audio_buffer(false), m_av_sync_started(false), m_current_index(-1), m_is_loading(false) {
	// Setup FIFO/Pipe for audio
	char fifo_path[256];
	#ifdef _WIN32
	snprintf(fifo_path, sizeof(fifo_path), "\\\\.\\pipe\\obs_mpv_audio_%p", this);
	m_fifo_path = fifo_path;
	// We create the pipe here, MPV will open it.
	// In Windows, the server (us) creates the pipe.
	m_pipe_handle = CreateNamedPipeA(
	m_fifo_path.c_str(),
	PIPE_ACCESS_INBOUND,
	PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
	1,
	4096 * 16,
	4096 * 16,
	0,
	NULL
	);
	#else
	snprintf(fifo_path, sizeof(fifo_path), "/tmp/obs_mpv_audio_%p", this);
	m_fifo_path = fifo_path;
	mkfifo(m_fifo_path.c_str(), 0666);
	#endif

	m_mpv = mpv_create();
	mpv_request_log_messages(m_mpv, "info");

	// Get OBS audio sample rate
	obs_audio_info oai;
	if (obs_get_audio_info(&oai)) {
		m_sample_rate = oai.samples_per_sec;
	} else {
		m_sample_rate = 48000; // Fallback
	}

	mpv_set_option_string(m_mpv, "vo", "libmpv");
	mpv_set_option_string(m_mpv, "hwdec", "auto");

	// Configure Audio to FIFO (RAW PCM)
	mpv_set_option_string(m_mpv, "ao", "pcm");
	mpv_set_option_string(m_mpv, "ao-pcm-file", m_fifo_path.c_str());
	mpv_set_option_string(m_mpv, "ao-pcm-format", "f32le");
	mpv_set_option_string(m_mpv, "ao-pcm-rate", std::to_string(m_sample_rate).c_str());
	mpv_set_option_string(m_mpv, "ao-pcm-channels", "stereo");
	mpv_set_option_string(m_mpv, "audio-format", "float");

	mpv_initialize(m_mpv);

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
	if (m_pipe_handle != INVALID_HANDLE_VALUE) {
		DisconnectNamedPipe(m_pipe_handle);
		CloseHandle(m_pipe_handle);
	}
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
	// On Windows, we already created the pipe. Wait for client (MPV) to connect.
	if (m_pipe_handle == INVALID_HANDLE_VALUE) return;

	BOOL connected = ConnectNamedPipe(m_pipe_handle, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

	while (!m_stop_audio_thread && connected) {
		if (m_flush_audio_buffer) {
			// Set a short timeout to drain the pipe without blocking forever
			COMMTIMEOUTS timeouts = {0};
			timeouts.ReadIntervalTimeout = MAXDWORD;
			timeouts.ReadTotalTimeoutConstant = 1; // 1ms timeout for reads
			timeouts.ReadTotalTimeoutMultiplier = 0;
			SetCommTimeouts(m_pipe_handle, &timeouts);

			DWORD bytes_read_drain = 0;
			while (ReadFile(m_pipe_handle, buf.data(), (DWORD)chunk_size, &bytes_read_drain, NULL) && bytes_read_drain > 0);
			m_flush_audio_buffer = false;

			// Restore original pipe mode (blocking)
			timeouts.ReadTotalTimeoutConstant = 0; // Blocking read
			SetCommTimeouts(m_pipe_handle, &timeouts);
		}

		DWORD bytes_read = 0;
		BOOL success = ReadFile(m_pipe_handle, buf.data(), (DWORD)chunk_size, &bytes_read, NULL);

		if (success && bytes_read > 0) {
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
					// Drain the non-blocking pipe
					while (read(fd, buf.data(), chunk_size) > 0);
					m_flush_audio_buffer = false;
				}

				ssize_t bytes_read = read(fd, buf.data(), chunk_size);
				if (bytes_read > 0) {
					#endif
					if (!m_av_sync_started) {
						// Discard audio until the first video frame is rendered
						continue;
					}

					uint32_t frames = (uint32_t)(bytes_read / sizeof(float) / 2);

					struct obs_source_audio audio = {};
					audio.samples_per_sec = m_sample_rate;
					audio.speakers = SPEAKERS_STEREO;
					audio.format = AUDIO_FORMAT_FLOAT;
					audio.data[0] = buf.data();
					audio.frames = frames;

					audio.timestamp = m_audio_start_ts + util_mul_div64(m_total_audio_frames, 1000000000ULL, m_sample_rate);

					m_total_audio_frames += frames;

					obs_source_output_audio(m_source, &audio);
				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			}

			#ifdef _WIN32
			DisconnectNamedPipe(m_pipe_handle);
			#else
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
					int obs_level = LOG_DEBUG;
					if (msg->log_level <= MPV_LOG_LEVEL_ERROR) obs_level = LOG_ERROR;
					else if (msg->log_level <= MPV_LOG_LEVEL_WARN) obs_level = LOG_WARNING;
					else if (msg->log_level <= MPV_LOG_LEVEL_INFO) obs_level = LOG_INFO;
					blog(obs_level, "[libmpv] %s: %s", msg->prefix, msg->text);
				}

				if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
					int64_t w, h;
					mpv_get_property(m_mpv, "width", MPV_FORMAT_INT64, &w);
					mpv_get_property(m_mpv, "height", MPV_FORMAT_INT64, &h);
					m_width = (uint32_t)w; m_height = (uint32_t)h;
				} else if (event->event_id == MPV_EVENT_AUDIO_RECONFIG) {
					int64_t new_rate = 0;
					if (mpv_get_property(m_mpv, "audio-params/samplerate", MPV_FORMAT_INT64, &new_rate) == 0 && new_rate > 0) {
						if (m_sample_rate != (uint32_t)new_rate) {
							blog(LOG_INFO, "[obs-mpv] Audio sample rate changed from %u to %" PRId64, m_sample_rate, new_rate);
							m_sample_rate = (uint32_t)new_rate;
						}
					}
				}
				if (event->event_id == MPV_EVENT_FILE_LOADED) {
					obs_log(LOG_INFO, "MPV: File Loaded");
					m_is_loading = false; // Loading finished
					tracks_changed = true;
					m_total_audio_frames = 0; // Reset A/V sync on new file
					m_audio_start_ts = 0;
					m_av_sync_started = false;

					int64_t new_rate = 0;
					if (mpv_get_property(m_mpv, "audio-params/samplerate", MPV_FORMAT_INT64, &new_rate) == 0 && new_rate > 0) {
						if (m_sample_rate != (uint32_t)new_rate) {
							blog(LOG_INFO, "[obs-mpv] Audio sample rate set to %" PRId64, new_rate);
							m_sample_rate = (uint32_t)new_rate;
						}
					}

					if(m_current_index >= 0 && (size_t)m_current_index < m_playlist.size()) {
						auto& item = m_playlist[m_current_index];
						if (item.last_seek_pos > 0) {
							seek(item.last_seek_pos);
						}
					}
				}
				if (event->event_id == MPV_EVENT_END_FILE) {
					auto end_ev = static_cast<mpv_event_end_file*>(event->data);
					obs_log(LOG_INFO, "MPV: End File (Reason: %d)", end_ev->reason);

					if(m_current_index >= 0 && (size_t)m_current_index < m_playlist.size()) {
						m_playlist[m_current_index].last_seek_pos = 0; // Reset seek pos
					}

					// Only proceed if natural EOF
					if (end_ev->reason == MPV_END_FILE_REASON_EOF) {
						playlist_next();
					}
				}
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

		void ObsMpvSource::playlist_add(const std::string& path) {
			playlist_add_multiple({path});
		}

		void ObsMpvSource::playlist_add_multiple(const std::vector<std::string>& paths) {
			obs_log(LOG_INFO, "Adding %zu files to playlist", paths.size());
			mpv_handle *probe_mpv = mpv_create();
			if (!probe_mpv) return;

			mpv_set_option_string(probe_mpv, "vo", "null");
			mpv_set_option_string(probe_mpv, "ao", "null");
			mpv_set_option_string(probe_mpv, "idle", "yes");
			if (mpv_initialize(probe_mpv) < 0) {
				mpv_terminate_destroy(probe_mpv);
				return;
			}

			for (const auto& path : paths) {
				FileMetadata meta = {0.0, 0.0, 0, {}, {}};
				const char *cmd[] = {"loadfile", path.c_str(), nullptr};
				mpv_command(probe_mpv, cmd);

				while (true) {
					mpv_event *event = mpv_wait_event(probe_mpv, 1.0);
					if (event->event_id == MPV_EVENT_FILE_LOADED) {
						mpv_get_property(probe_mpv, "duration", MPV_FORMAT_DOUBLE, &meta.duration);
						mpv_get_property(probe_mpv, "container-fps", MPV_FORMAT_DOUBLE, &meta.fps);
						mpv_get_property(probe_mpv, "audio-params/channel-count", MPV_FORMAT_INT64, &meta.channels);

						mpv_node node;
						if (mpv_get_property(probe_mpv, "track-list", MPV_FORMAT_NODE, &node) == 0) {
							if (node.format == MPV_FORMAT_NODE_ARRAY) {
								for (int i = 0; i < node.u.list->num; i++) {
									mpv_node *tr = &node.u.list->values[i];
									const char *t=nullptr, *l=nullptr; int64_t id=-1;
									for (int j=0; j<tr->u.list->num; j++) {
										const char *k = tr->u.list->keys[j]; mpv_node *v = &tr->u.list->values[j];
										if (!strcmp(k, "type")) t=v->u.string;
										else if (!strcmp(k, "id")) id=v->u.int64;
										else if (!strcmp(k, "lang")) l=v->u.string;
									}
									if (t && !strcmp(t, "audio")) meta.audio_tracks.push_back({(int)id, l ? l : std::to_string(id), false});
									if (t && !strcmp(t, "sub")) meta.sub_tracks.push_back({(int)id, l ? l : std::to_string(id), false});
								}
							}
							mpv_free_node_contents(&node);
						}
						break;
					}
					if (event->event_id == MPV_EVENT_SHUTDOWN || event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_END_FILE) {
						// Failed to load or end
						if (event->event_id != MPV_EVENT_FILE_LOADED) break;
					}
				}

				PlaylistItem item;
				item.path = path;
				item.duration = meta.duration;
				item.fps = meta.fps;
				item.audio_channels = (int)meta.channels;
				item.audio_tracks = meta.audio_tracks;
				item.sub_tracks = meta.sub_tracks;
				size_t last_slash = path.find_last_of("/\\");
				item.name = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
				m_playlist.push_back(item);
			}

			mpv_terminate_destroy(probe_mpv);
		}

		void ObsMpvSource::playlist_remove(int index) {
			if (index >= 0 && (size_t)index < m_playlist.size()) {
				m_playlist.erase(m_playlist.begin() + index);
				if (index == m_current_index) {
					stop();
					m_current_index = -1;
				} else if (index < m_current_index) {
					m_current_index--;
				}
			}
		}

		void ObsMpvSource::playlist_move(int from, int to) {
			if (from >= 0 && (size_t)from < m_playlist.size() && to >= 0 && (size_t)to < m_playlist.size() && from != to) {
				auto item = m_playlist[from];
				m_playlist.erase(m_playlist.begin() + from);
				m_playlist.insert(m_playlist.begin() + to, item);

				// Update current index if moved item is currently playing
				if (m_current_index == from) {
					m_current_index = to;
				}
			}
		}

		void ObsMpvSource::set_auto_obs_fps(bool enabled) { m_auto_obs_fps = enabled; }
		bool ObsMpvSource::get_auto_obs_fps() { return m_auto_obs_fps; }

		void ObsMpvSource::playlist_play(int index) {
			if (index >= 0 && (size_t)index < m_playlist.size()) {
				m_flush_audio_buffer = true;
				obs_log(LOG_INFO, "Playlist Play request: index %d", index);
				m_is_loading = true; // Start blackout
				m_current_index = index;
				auto& item = m_playlist[index];

				const char *cmd[] = {"loadfile", item.path.c_str(), nullptr};
				mpv_command_async(m_mpv, 0, cmd);

				if (m_auto_obs_fps && item.fps > 0) {
					obs_video_info ovi;
					if (obs_get_video_info(&ovi)) {
						// Approximate fraction
						uint32_t num = (uint32_t)(item.fps * 100000.0 + 0.5);
						uint32_t den = 100000;

						// Common rates check to get exact standard values
						if (std::abs(item.fps - 60.0) < 0.01) { num=60; den=1; }
						else if (std::abs(item.fps - 30.0) < 0.01) { num=30; den=1; }
						else if (std::abs(item.fps - 50.0) < 0.01) { num=50; den=1; }
						else if (std::abs(item.fps - 25.0) < 0.01) { num=25; den=1; }
						else if (std::abs(item.fps - 24.0) < 0.01) { num=24; den=1; }
						else if (std::abs(item.fps - 59.94) < 0.01) { num=60000; den=1001; }
						else if (std::abs(item.fps - 29.97) < 0.01) { num=30000; den=1001; }
						else if (std::abs(item.fps - 23.976) < 0.01) { num=24000; den=1001; }

						if (ovi.fps_num != num || ovi.fps_den != den) {
							ovi.fps_num = num;
							ovi.fps_den = den;
							obs_log(LOG_INFO, "Auto-matching OBS FPS to %.2f (%u/%u)", item.fps, num, den);
							obs_reset_video(&ovi);
						}
					}
				}

				// Force start playback
				mpv_set_property_string(m_mpv, "pause", "no");

				// Apply saved settings
				set_volume(item.volume);

				if (item.audio_track < 0) mpv_set_property_string(m_mpv, "aid", "no");
				else mpv_set_property_string(m_mpv, "aid", std::to_string(item.audio_track).c_str());

				if (item.sub_track < 0) mpv_set_property_string(m_mpv, "sid", "no");
				else mpv_set_property_string(m_mpv, "sid", std::to_string(item.sub_track).c_str());

				mpv_set_property_string(m_mpv, "loop-file", item.loop ? "inf" : "no");

				// Audio Fades
				std::string filters = "";
				if (item.fade_in_enabled && item.fade_in > 0) {
					filters += "lavfi=[afade=t=in:st=0:d="+ std::to_string(item.fade_in) + "]";
				}
				if (item.fade_out_enabled && item.fade_out > 0 && item.duration > item.fade_out) {
					if (!filters.empty()) filters += ",";
					double start_time = item.duration - item.fade_out;
					filters += "lavfi=[afade=t=out:st="+ std::to_string(start_time) + ":d="+ std::to_string(item.fade_out) + "]";
				}
				if (!filters.empty()) {
					mpv_set_property_string(m_mpv, "af", filters.c_str());
				} else {
					mpv_set_property_string(m_mpv, "af", "");
				}

				if (!item.ext_sub_path.empty()) {
					const char *sub_cmd[] = {"sub-add", item.ext_sub_path.c_str(), "select", nullptr};
					mpv_command_async(m_mpv, 0, sub_cmd);
				}
			}
		}

		void ObsMpvSource::playlist_next() {
			obs_log(LOG_INFO, "Playlist Next triggered. Current: %d", m_current_index);
			if (m_current_index >= 0 && (size_t)m_current_index < m_playlist.size()) {
				if (m_playlist[m_current_index].loop) {
					playlist_play(m_current_index); // Loop current file
					return;
				}
			}

			if ((size_t)(m_current_index + 1) < m_playlist.size()) {
				playlist_play(m_current_index + 1);
			} else {
				m_current_index = -1;
				// End of playlist
			}
		}

		ObsMpvSource::PlaylistItem* ObsMpvSource::playlist_get_item(int index) {
			if (index >= 0 && (size_t)index < m_playlist.size()) {
				return &m_playlist[index];
			}
			return nullptr;
		}

		int ObsMpvSource::playlist_count() {
			return (int)m_playlist.size();
		}

		void ObsMpvSource::play() { mpv_set_property_string(m_mpv, "pause", "no"); }
		void ObsMpvSource::pause() { mpv_set_property_string(m_mpv, "pause", "yes"); }
		void ObsMpvSource::stop() {
			mpv_command_string(m_mpv, "stop");
		}
		void ObsMpvSource::seek(double s) { mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &s); }
		double ObsMpvSource::get_time_pos() { double v=0; mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &v); return v; }
		double ObsMpvSource::get_duration() { double v=0; mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &v); return v; }
		double ObsMpvSource::get_volume() { double v=100; mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &v); return v; }
		void ObsMpvSource::set_volume(double vol) { mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol); }

		bool ObsMpvSource::is_playing() {
			int p=1;
			mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
			return p==0 && !is_idle();
		}

		bool ObsMpvSource::is_paused(){
			int p=0;
			mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
			return p==1 && !is_idle();
		}

		bool ObsMpvSource::is_idle(){
			int idle=0;
			mpv_get_property(m_mpv, "idle-active", MPV_FORMAT_FLAG, &idle);
			return idle==1;
		}

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
		enum obs_media_state ObsMpvSource::obs_media_get_state(void *d) {
			auto s = static_cast<ObsMpvSource*>(d);
			if (s->is_playing()) return OBS_MEDIA_STATE_PLAYING;
			if (s->is_paused()) return OBS_MEDIA_STATE_PAUSED;
			return OBS_MEDIA_STATE_STOPPED;
		}

		uint32_t ObsMpvSource::obs_get_width(void *d) { return static_cast<ObsMpvSource*>(d)->m_width; }
		uint32_t ObsMpvSource::obs_get_height(void *d) { return static_cast<ObsMpvSource*>(d)->m_height; }

		void ObsMpvSource::obs_save(void *data, obs_data_t *settings) {
			static_cast<ObsMpvSource*>(data)->save_playlist(settings);
		}

		void ObsMpvSource::save_playlist(obs_data_t *settings) {
			obs_data_set_bool(settings, "auto_obs_fps", m_auto_obs_fps);
			obs_data_array_t *array = obs_data_array_create();
			for (const auto& item : m_playlist) {
				obs_data_t *obj = obs_data_create();
				obs_data_set_string(obj, "path", item.path.c_str());
				obs_data_set_string(obj, "name", item.name.c_str());
				obs_data_set_double(obj, "duration", item.duration);
				obs_data_set_double(obj, "volume", item.volume);
				obs_data_set_bool(obj, "loop", item.loop);
				obs_data_set_int(obj, "audio_track", item.audio_track);
				obs_data_set_int(obj, "sub_track", item.sub_track);
				obs_data_set_string(obj, "ext_sub_path", item.ext_sub_path.c_str());
				obs_data_set_bool(obj, "fade_in_enabled", item.fade_in_enabled);
				obs_data_set_double(obj, "fade_in", item.fade_in);
				obs_data_set_bool(obj, "fade_out_enabled", item.fade_out_enabled);
				obs_data_set_double(obj, "fade_out", item.fade_out);

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
				item.duration = obs_data_get_double(obj, "duration");
				item.volume = obs_data_get_double(obj, "volume");
				item.loop = obs_data_get_bool(obj, "loop");
				item.audio_track = (int)obs_data_get_int(obj, "audio_track");
				item.sub_track = (int)obs_data_get_int(obj, "sub_track");
				item.ext_sub_path = obs_data_get_string(obj, "ext_sub_path");
				item.fade_in_enabled = obs_data_get_bool(obj, "fade_in_enabled");
				item.fade_in = obs_data_get_double(obj, "fade_in");
				item.fade_out_enabled = obs_data_get_bool(obj, "fade_out_enabled");
				item.fade_out = obs_data_get_double(obj, "fade_out");

				// Re-probe tracks for metadata
				mpv_handle *probe_mpv = mpv_create();
				if (probe_mpv) {
					mpv_set_option_string(probe_mpv, "vo", "null");
					mpv_set_option_string(probe_mpv, "ao", "null");
					mpv_initialize(probe_mpv);
					const char *cmd[] = {"loadfile", item.path.c_str(), nullptr};
					mpv_command(probe_mpv, cmd);
					while (true) {
						mpv_event *event = mpv_wait_event(probe_mpv, 0.5);
						if (event->event_id == MPV_EVENT_FILE_LOADED) {
							mpv_get_property(probe_mpv, "container-fps", MPV_FORMAT_DOUBLE, &item.fps);
							int64_t ch = 0;
							mpv_get_property(probe_mpv, "audio-params/channel-count", MPV_FORMAT_INT64, &ch);
							item.audio_channels = (int)ch;

							mpv_node node;
							if (mpv_get_property(probe_mpv, "track-list", MPV_FORMAT_NODE, &node) == 0) {
								if (node.format == MPV_FORMAT_NODE_ARRAY) {
									for (int k = 0; k < node.u.list->num; k++) {
										mpv_node *tr = &node.u.list->values[k];
										const char *t=nullptr, *l=nullptr; int64_t tid=-1;
										for (int j=0; j<tr->u.list->num; j++) {
											const char *key = tr->u.list->keys[j]; mpv_node *v = &tr->u.list->values[j];
											if (!strcmp(key, "type")) t=v->u.string;
											else if (!strcmp(key, "id")) tid=v->u.int64;
											else if (!strcmp(key, "lang")) l=v->u.string;
										}
										if (t && !strcmp(t, "audio")) item.audio_tracks.push_back({(int)tid, l ? l : std::to_string(tid), false});
										if (t && !strcmp(t, "sub")) item.sub_tracks.push_back({(int)tid, l ? l : std::to_string(tid), false});
									}
								}
								mpv_free_node_contents(&node);
							}
							break;
						}
						if (event->event_id == MPV_EVENT_NONE || event->event_id == MPV_EVENT_SHUTDOWN) break;
					}
					mpv_terminate_destroy(probe_mpv);
				}

				m_playlist.push_back(item);
				obs_data_release(obj);
			}
			obs_data_array_release(array);
		}
