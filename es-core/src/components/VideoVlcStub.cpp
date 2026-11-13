// libVLC's functions that ES calls, doing nothing, for the builds without libVLC (ES_NO_VLC: the
// MiSTer's, whose ARM cannot decode video beside the menus). libvlc_new returns nothing, so every video
// component shows its picture in place of the video, as it does when VLC cannot start.
#ifdef ES_NO_VLC

#include <vlc/vlc.h>

extern "C"
{
	libvlc_instance_t* libvlc_new(int, const char* const*) { return nullptr; }
	void libvlc_release(libvlc_instance_t*) { }
	const char* libvlc_get_version(void) { return "none"; }

	libvlc_media_t* libvlc_media_new_path(libvlc_instance_t*, const char*) { return nullptr; }
	void libvlc_media_add_option(libvlc_media_t*, const char*) { }
	void libvlc_media_release(libvlc_media_t*) { }
	void libvlc_media_parse(libvlc_media_t*) { }
	int libvlc_media_parse_with_options(libvlc_media_t*, libvlc_media_parse_flag_t, int) { return -1; }
	libvlc_media_parsed_status_t libvlc_media_get_parsed_status(libvlc_media_t*) { return libvlc_media_parsed_status_failed; }
	unsigned libvlc_media_tracks_get(libvlc_media_t*, libvlc_media_track_t*** tracks) { if (tracks) *tracks = nullptr; return 0; }
	void libvlc_media_tracks_release(libvlc_media_track_t**, unsigned) { }

	libvlc_media_player_t* libvlc_media_player_new_from_media(libvlc_media_t*) { return nullptr; }
	void libvlc_media_player_release(libvlc_media_player_t*) { }
	int libvlc_media_player_play(libvlc_media_player_t*) { return -1; }
	void libvlc_media_player_pause(libvlc_media_player_t*) { }
	void libvlc_media_player_stop(libvlc_media_player_t*) { }
	void libvlc_media_player_set_media(libvlc_media_player_t*, libvlc_media_t*) { }
	libvlc_time_t libvlc_media_player_get_time(libvlc_media_player_t*) { return 0; }
	void libvlc_media_player_set_time(libvlc_media_player_t*, libvlc_time_t) { }
	void libvlc_media_player_set_position(libvlc_media_player_t*, float) { }
	int libvlc_media_player_set_rate(libvlc_media_player_t*, float) { return -1; }
	libvlc_state_t libvlc_media_player_get_state(libvlc_media_player_t*) { return libvlc_Error; }
	void libvlc_audio_set_mute(libvlc_media_player_t*, int) { }

	void libvlc_video_set_format(libvlc_media_player_t*, const char*, unsigned, unsigned, unsigned) { }
	void libvlc_video_set_callbacks(libvlc_media_player_t*, libvlc_video_lock_cb, libvlc_video_unlock_cb, libvlc_video_display_cb, void*) { }
	int libvlc_video_take_snapshot(libvlc_media_player_t*, unsigned, const char*, unsigned int, unsigned int) { return -1; }
}

#endif
