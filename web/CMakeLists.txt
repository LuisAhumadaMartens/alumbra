add_executable(alumbra_web_bindings bindings_entry.cc)
target_link_libraries(alumbra_web_bindings PRIVATE umbra::c_api)
target_compile_options(alumbra_web_bindings PRIVATE -fno-exceptions)
alumbra_target_options(alumbra_web_bindings)
set_target_properties(alumbra_web_bindings PROPERTIES SUFFIX ".mjs")
set(_alumbra_web_exports
  "['_main','_malloc','_free','_alumbra_c_api_version','_alumbra_negotiate_version','_alumbra_player_create','_alumbra_player_destroy','_alumbra_player_get_source','_alumbra_player_set_source','_alumbra_player_load','_alumbra_player_play','_alumbra_player_pause','_alumbra_player_fast_seek','_alumbra_player_can_play_type','_alumbra_player_get_paused','_alumbra_player_get_ended','_alumbra_player_get_autoplay','_alumbra_player_set_autoplay','_alumbra_player_get_loop','_alumbra_player_set_loop','_alumbra_player_get_duration','_alumbra_player_get_preload','_alumbra_player_set_preload','_alumbra_player_get_default_playback_rate','_alumbra_player_set_default_playback_rate','_alumbra_player_get_preserves_pitch','_alumbra_player_set_preserves_pitch','_alumbra_player_get_time_ranges','_alumbra_player_get_time_range','_alumbra_player_get_current_time','_alumbra_player_set_current_time','_alumbra_player_get_seeking','_alumbra_player_get_volume','_alumbra_player_set_volume','_alumbra_player_get_muted','_alumbra_player_set_muted','_alumbra_player_get_playback_rate','_alumbra_player_set_playback_rate','_alumbra_player_get_ready_state','_alumbra_player_get_network_state','_alumbra_player_get_statistics','_alumbra_player_get_error','_alumbra_player_poll_event','_alumbra_settings_get','_alumbra_settings_set','_alumbra_player_text_track_count','_alumbra_player_text_track_at','_alumbra_player_add_text_track','_alumbra_text_track_release','_alumbra_text_track_get_kind','_alumbra_text_track_get_mode','_alumbra_text_track_set_mode','_alumbra_text_track_get_id','_alumbra_text_track_get_label','_alumbra_text_track_get_language']")
target_link_options(alumbra_web_bindings PRIVATE
  "-sEXPORTED_FUNCTIONS=${_alumbra_web_exports}"
  "-sEXPORTED_RUNTIME_METHODS=['addFunction','removeFunction','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
  -sALLOW_TABLE_GROWTH=1
  -sMODULARIZE
  -sEXPORT_ES6
  -sNO_EXIT_RUNTIME=1)
