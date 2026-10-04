#include "patterns.h"

namespace patterns
{
    const ::addresses::entry add_entity{ "add_entity", "client.dll:488D05*????????48890733D24088B728210000+78~" };
    const ::addresses::entry base_fire_guns_get_inaccuracy{ "base_fire_guns_get_inaccuracy", "client.dll:44894C24205556415441554156488DAC2440FFFFFF4881ECE00100000F29B424D0010000458BE9" };
    const ::addresses::entry button_state_alloc{ "button_state_alloc", "client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF-80" };
    const ::addresses::entry cmd_interpreter{ "cmd_interpreter", "rendersystemdx11.dll:>E8????????4183BDC000000000" };
    const ::addresses::entry create_move{ "create_move", "client.dll:FFFFFFFF488D05*????????48890D????????+28~" };
    const ::addresses::entry csgo_input{ "csgo_input", "client.dll:84C0740C488D0D*????????E8????????" };
    const ::addresses::entry draw_flash_effect{ "draw_flash_effect", "client.dll:85D20F88????????48894C24??5556" };
    const ::addresses::entry draw_legs{ "draw_legs", "client.dll:4C8BDC555356574157498DAB????????4881EC????????8B42??4D8BF9F20F1042" };
    const ::addresses::entry draw_overhead{ "draw_overhead", "client.dll:40534883EC??488BD983FA??75??" };
    const ::addresses::entry draw_scene_object{ "draw_scene_object", "scenesystem.dll:488D05*????????488907488B7C2448+8~" };
    const ::addresses::entry draw_scene_object_array{ "draw_scene_object_array", "scenesystem.dll:488BC4488950??488948??555356574154415541564157488DA8????????4881EC????????0F2970??" };
    const ::addresses::entry draw_skybox_array{ "draw_skybox_array", "scenesystem.dll:4585C90F8E????????4C8BDC" };
    const ::addresses::entry dynamic_light_alloc{ "dynamic_light_alloc", "client.dll:488BD94533C0488B0D????????BA01000000>E8????????8B0B" };
    const ::addresses::entry dynamic_light_manager{ "dynamic_light_manager", "client.dll:488B0D*????????4885C97408488BD7E8????????E8" };
    const ::addresses::entry dynamic_light_time{ "dynamic_light_time", "client.dll:40534883EC20488BD985D2741A488B05????????F30F104830" };
    const ::addresses::entry engine_client_cmd{ "engine_client_cmd", "engine2.dll:488BC448895808488968104889701857415641574881EC????????0F2970D8410FB6E9" };
    const ::addresses::entry entity_list{ "entity_list", "client.dll:488B0D*????????8BFBC1EB0E" };
    const ::addresses::entry filesystem_close{ "filesystem_close", "filesystem_stdio.dll:488D8F20FFFFFF8BD5>E8????????FFD3" };
    const ::addresses::entry find_hud_element{ "find_hud_element", "client.dll:40534883EC??488B05????????488BD94885C074??48895C24" };
    const ::addresses::entry frame_input_ring_base{ "frame_input_ring_base", "engine2.dll:488D05*????????0F1004C8" };
    const ::addresses::entry frame_input_ring_idx{ "frame_input_ring_idx", "engine2.dll:486315*????????83FA0A7D61" };
    const ::addresses::entry frame_stage_notify{ "frame_stage_notify", "client.dll:48895C241848896C2420574883EC40488BF9" };
    const ::addresses::entry game_entity_system{ "game_entity_system", "client.dll:488B0D*????????EB028BC6" };
    const ::addresses::entry game_event_get_controller{ "game_event_get_controller", "client.dll:488D05*????????4D8BF8488901+80~" };
    const ::addresses::entry game_event_get_float{ "game_event_get_float", "client.dll:>E8????????0F28D8895C2420" };
    const ::addresses::entry game_event_get_int{ "game_event_get_int", "client.dll:>E8????????3D00800000" };
    const ::addresses::entry game_event_get_pawn{ "game_event_get_pawn", "client.dll:488D05*????????4D8BF8488901+88~" };
    const ::addresses::entry game_event_get_string{ "game_event_get_string", "client.dll:>E8????????85DB0F9FC3" };
    const ::addresses::entry game_event_manager{ "game_event_manager", "client.dll:488B0D*????????488B01FF50??FFC3~" };
    const ::addresses::entry game_rules{ "game_rules", "client.dll:488B0D*????????4C897010" };
    const ::addresses::entry game_scene_node_set_mesh_group{ "game_scene_node_set_mesh_group", "client.dll:>E8????????8B852C850100" };
    const ::addresses::entry game_scene_node_set_skeleton{ "game_scene_node_set_skeleton", "client.dll:>E8????????4084ED7417" };
    const ::addresses::entry game_trace_manager{ "game_trace_manager", "client.dll:488B0D*????????488D3452~" };
    const ::addresses::entry generate_primitives{ "generate_primitives", "scenesystem.dll:488D05*????????488907488B7C2448+20~" };
    const ::addresses::entry get_aim_punch{ "get_aim_punch", "client.dll:150000488D542420>E8????????F30F1015????????" };
    const ::addresses::entry get_bone_index{ "get_bone_index", "client.dll:448B42??488B12E9" };
    const ::addresses::entry get_glow_color{ "get_glow_color", "client.dll:>E8????????F30F10BE????????488BCF" };
    const ::addresses::entry get_inaccuracy{ "get_inaccuracy", "client.dll:48895C24??5556574881EC????????440F298424" };
    const ::addresses::entry get_interp_amount{ "get_interp_amount", "client.dll:>E8????????418B9668030000" };
    const ::addresses::entry get_interpolated_shoot_position{ "get_interpolated_shoot_position", "client.dll:40555641564881EC20010000" };
    const ::addresses::entry get_spread{ "get_spread", "client.dll:486391????????488B81????????85D278??4883FA0273??F30F10849050070000C3F30F108050070000C3" };
    const ::addresses::entry get_net_channel{ "get_net_channel", "engine2.dll:4C8B05????????4D85C07410" };
    const ::addresses::entry get_tick_view_angles{ "get_tick_view_angles", "client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04" };
    const ::addresses::entry get_transforms_for_hitbox_list{ "get_transforms_for_hitbox_list", "client.dll:48895C24??555657415441554881EC????????4963304D8BE0488BEA488BD985F6" };
    const ::addresses::entry get_usercmd{ "get_usercmd", "client.dll:40534883EC208BDAE8????????4C8BC0" };
    const ::addresses::entry get_usercmd_base{ "get_usercmd_base", "client.dll:4883EC28>E8????????8B8010590000" };
    const ::addresses::entry get_view_angles{ "get_view_angles", "client.dll:8B0D????????8BD3>E8????????F20F1000" };
    const ::addresses::entry get_world_group_handle{ "get_world_group_handle", "client.dll:>E8????????418B5F10" };
    const ::addresses::entry get_world_group_id{ "get_world_group_id", "client.dll:488B41304885C0740D488B40108B4838488BC2890AC3" };
    const ::addresses::entry global_vars{ "global_vars", "client.dll:488B05*????????448B4044" };
    const ::addresses::entry handle_view_angles{ "handle_view_angles", "client.dll:FFFFFFFF488D05*????????48890D????????+40~" };
    const ::addresses::entry history_field_alloc{ "history_field_alloc", "client.dll:>E8????????488BD0488D4E28" };
    const ::addresses::entry hud{ "hud", "client.dll:488B05*????????4885C07471" };
    const ::addresses::entry hud_death_notice_clear{ "hud_death_notice_clear", "client.dll:85C07509488D4EE0>E8????????488B5C2440" };
    const ::addresses::entry hud_weapon_selection_update{ "hud_weapon_selection_update", "client.dll:498BE35FC3488BCB>E8????????488BCBE8????????" };
    const ::addresses::entry init_particle_path_buffer{ "init_particle_path_buffer", "client.dll:48895C24??574883EC??8B41??488D79" };
    const ::addresses::entry init_particle_path_buffer_alt{ "init_particle_path_buffer_alt", "client.dll:48895C24??574883EC??8B41??488D79" };
    const ::addresses::entry is_glowing{ "is_glowing", "client.dll:0000488BEA488BF9>E8????????4533F684C0" };
    const ::addresses::entry item_system{ "item_system", "client.dll:4883EC28488B05????????4885C00F8581" };
    const ::addresses::entry kv3_alloc{ "kv3_alloc", "tier0.dll:40534883EC3080FA060FB6C241B916" };
    const ::addresses::entry kv3_destroy{ "kv3_destroy", "tier0.dll:405741574883EC384C8B01448BFA498BC0488BF948C1E802" };
    const ::addresses::entry kv3_load{ "kv3_load", "tier0.dll:44242848897C2420>E8????????0FB6D88B4C2444" };
    const ::addresses::entry level_initialization{ "level_initialization", "client.dll:488D05*????????C6411000+B8~" };
    const ::addresses::entry level_shutdown{ "level_shutdown", "client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??" };
    const ::addresses::entry light_data_queue{ "light_data_queue", "scenesystem.dll:488B05*????????48C1E104+8" };
    const ::addresses::entry light_scene_object{ "light_scene_object", "scenesystem.dll:>E8????????440F285C2460" };
    const ::addresses::entry local_player_controller{ "local_player_controller", "client.dll:48391D*????????7504B001" };
    const ::addresses::entry log_internal{ "log_internal", "tier0.dll:>E8????????448B55B3" };
    const ::addresses::entry material_create{ "material_create", "materialsystem2.dll:48895C24??48896C24??48897424??48897C24??41564881EC????????488B05????????488BF2" };
    const ::addresses::entry material_manager{ "material_manager", "client.dll:488B0D*????????80A5E7000000EF" };
    const ::addresses::entry override_view{ "override_view", "client.dll:A8000000488D05*????????4C89742420+78~" };
    const ::addresses::entry parse_report_hit{ "parse_report_hit", "client.dll:488D05*????????488D5424304889442450488D4C2450E8????????488378180F76??488B0041B96C010000~" };
    const ::addresses::entry particle_create_effect{ "particle_create_effect", "client.dll:4C8BDC534881EC90000000F20F1005" };
    const ::addresses::entry particle_destroy_effect{ "particle_destroy_effect", "client.dll:83FAFF0F84????????4154" };
    const ::addresses::entry particle_manager{ "particle_manager", "client.dll:488B35*????????44896C24??" };
    const ::addresses::entry particle_set_control_point{ "particle_set_control_point", "client.dll:4883EC58F3410F105104F3410F1009F3410F105908" };
    const ::addresses::entry particle_set_entity_binding{ "particle_set_entity_binding", "client.dll:4154415541574881EC900000004D8BF9" };
    const ::addresses::entry particle_set_transform{ "particle_set_transform", "client.dll:48895C24??48896C24??48897424??574883EC40488BF9498BE9" };
    const ::addresses::entry planted_c4{ "planted_c4", "client.dll:488B1D*????????488BD34C8B81" };
    const ::addresses::entry post_network_data_received{ "post_network_data_received", "client.dll:48895C241048894C24085556574154415541564157488DAC2400FDFFFF4881EC000400004C8BF1488B0D????????488B01FF90B8000000488BC8488B10FF5238498BCE894424588BF0E8????????498BCEE8????????" };
    const ::addresses::entry prediction_player{ "prediction_player", "client.dll:488B4738488D0D????????488905*????????488D15????????83FD03" };
    const ::addresses::entry prediction_process_movement{ "prediction_process_movement", "client.dll:4D8BC6498BD5488BCF488BD8>E8????????FFD3" };
    const ::addresses::entry prediction_reset_pawn{ "prediction_reset_pawn", "client.dll:488B114885D274??806A????75??488B05????????8B4044894218" };
    const ::addresses::entry prediction_seed{ "prediction_seed", "client.dll:8B3D*????????488B03488BCB" };
    const ::addresses::entry prediction_set_pawn{ "prediction_set_pawn", "client.dll:48895C24??574883EC2048C70100000000488BFA488BD94885D274??488B02" };
    const ::addresses::entry prediction_set_state{ "prediction_set_state", "client.dll:8B81????????84D274??FFC08981????????C383E8018981????????75??80B9" };
    const ::addresses::entry prediction_setup_move{ "prediction_setup_move", "client.dll:498BCE458B4044>E8????????488B074D8BC6" };
    const ::addresses::entry prediction_state{ "prediction_state", "client.dll:488B0D*????????33D28B5B38" };
    const ::addresses::entry prepare_scene_material{ "prepare_scene_material", "materialsystem2.dll:48895C24084889742410574883EC30488B5920" };
    const ::addresses::entry process_input_event{ "process_input_event", "client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~" };
    const ::addresses::entry read_frame_input{ "read_frame_input", "client.dll:>E8????????4D8BC58BD3" };
    const ::addresses::entry remove_entity{ "remove_entity", "client.dll:488D05*????????48890733D24088B728210000+80~" };
    const ::addresses::entry render_crosshair{ "render_crosshair", "client.dll:488BC844886C2430>E8????????84C00F84????????" };
    const ::addresses::entry render_decals{ "render_decals", "client.dll:44884C2420555341544155488D6C24??4881EC????????4C8B15????????4C8BEA4C8BE1" };
    const ::addresses::entry render_game_system_storage{ "render_game_system_storage", "client.dll:488B0D*????????418BD6E8????????418B5F" };
    const ::addresses::entry render_scope{ "render_scope", "client.dll:488BC453574883EC68488BFA" };
    const ::addresses::entry render_smoke{ "render_smoke", "client.dll:5C24284889442420>E8????????488B5C2460" };
    const ::addresses::entry render_view{ "render_view", "client.dll:4C8BDC53555741554881ECD8000000488D05????????48C7442448C8010000" };
    const ::addresses::entry resource_system_load{ "resource_system_load", "resourcesystem.dll:48895C24??48896C24??48897424??574883EC??488B01" };
    const ::addresses::entry resource_system_precache{ "resource_system_precache", "resourcesystem.dll:405355574881EC80000000" };
    const ::addresses::entry serialize_move_crc{ "serialize_move_crc", "client.dll:48895C24??5556574883EC30498BC0488BFA488BF1488B09F6C103" };
    const ::addresses::entry service_read{ "service_read", "filesystem_stdio.dll:00488907488D05*????????488987E0000000~" };
    const ::addresses::entry set_info{ "set_info", "engine2.dll:40554157488D6C24??4881EC????????4533FF" };
    const ::addresses::entry set_player_model{ "set_player_model", "client.dll:488D15????????488BCB>E8????????488BD7488BCB" };
    const ::addresses::entry set_postprocess_vec{ "set_postprocess_vec", "engine2.dll:>E8????????440F289424" };
    const ::addresses::entry set_shader_param{ "set_shader_param", "client.dll:48896C24104889742418574883EC20660F6ECA498BF0660F70C9008BEA488BF94533C9488BC166660F1F840000000000660F6FC14C8D15????????660F76000F50C885C975??41FFC14883C0104183F90172??488B4738" };
    const ::addresses::entry set_shader_param_i{ "set_shader_param_i", "client.dll:48896C24??48897424??574883EC20660F6ECA418BF0" };
    const ::addresses::entry set_view_angles{ "set_view_angles", "client.dll:85D275??486381" };
    const ::addresses::entry set_voice_data{ "set_voice_data", "client.dll:>E8????????4C39B558140000" };
    const ::addresses::entry simulation_player{ "simulation_player", "client.dll:4C8B43384C3905*????????400F94C5" };
    const ::addresses::entry sort_primitives{ "sort_primitives", "scenesystem.dll:4585C90F84????????5556574883EC30" };
    const ::addresses::entry setup_fog{ "setup_fog", "client.dll:48895C24??48896C24??48897424??48894C24??5741544155415641574883EC20486302" };
    const ::addresses::entry play_sound{ "play_sound", "soundsystem.dll:4C8BDC55415541564157" };
    const ::addresses::entry string_copy{ "string_copy", "client.dll:>E8????????0F104588" };
    const ::addresses::entry subtick_move_alloc{ "subtick_move_alloc", "client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF" };
    const ::addresses::entry trace_bullet{ "trace_bullet", "client.dll:40535741564883EC508B8424" };
    const ::addresses::entry trace_bullet_data_init{ "trace_bullet_data_init", "client.dll:48895C24??48896C24??48897424??57415641574883EC??????????4D8D71" };
    const ::addresses::entry trace_bullet_free{ "trace_bullet_free", "client.dll:4055415541574883EC" };
    const ::addresses::entry trace_bullet_update{ "trace_bullet_update", "client.dll:48895C24??48896C24??48897424??574881EC????????488BE90F297424" };
    const ::addresses::entry trace_filter_init{ "trace_filter_init", "client.dll:48895C24??48897424??574883EC??0FB641??33FF24" };
    const ::addresses::entry trace_filter_set_collision{ "trace_filter_set_collision", "client.dll:000041B800010000>E8????????4C397F38" };
    const ::addresses::entry trace_hull{ "trace_hull", "client.dll:>E8????????0F2F754C" };
    const ::addresses::entry trace_ray{ "trace_ray", "client.dll:48895424??48894C24??55535657415441564157488DAC24????????B8????0000" };
    const ::addresses::entry trace_ray_entity{ "trace_ray_entity", "client.dll:488954241048894C240855535657415441564157488DAC24????????B8" };
    const ::addresses::entry update_fov_sensitivity{ "update_fov_sensitivity", "client.dll:48896C24??4889742418574883EC30488BB9????????488BF1488BCF48895C24" };
    const ::addresses::entry utl_vector_push{ "utl_vector_push", "client.dll:>E8????????4C8BD0458B4A10" };
    const ::addresses::entry view_matrix{ "view_matrix", "client.dll:488D0D*????????48C1E006" };
    const ::addresses::entry viewmodel_update_mesh{ "viewmodel_update_mesh", "client.dll:48895C24??48897424??574883EC??488D99????????488B71" };
    const ::addresses::entry weapon_calculate_spread{ "weapon_calculate_spread", "client.dll:28F3440F11442420>E8????????488D85B0000000" };
    const ::addresses::entry weapon_get_entity_index{ "weapon_get_entity_index", "client.dll:4883EC084C8B0D????????4C8BDA488B49104983C1104885C9750EC702FFFFFFFF488BC24883C408C3" };
    const ::addresses::entry weapon_get_model_path{ "weapon_get_model_path", "client.dll:48895C2410564883EC20488B1D????????" };
    const ::addresses::entry weapon_get_recoil_offset{ "weapon_get_recoil_offset", "client.dll:>E8????????488D44243C" };
    const ::addresses::entry weapon_get_viewmodel{ "weapon_get_viewmodel", "client.dll:40534883EC20488BD9E8????????4883BB8803000000" };
    const ::addresses::entry weapon_recoil_data{ "weapon_recoil_data", "client.dll:488D0D*????????488D8424????????41B801000000" };
    const ::addresses::entry weapon_set_mesh_group_mask{ "weapon_set_mesh_group_mask", "client.dll:48895C24??48897424??574883EC??488D99????????488B71" };
    const ::addresses::entry weapon_update_accuracy{ "weapon_update_accuracy", "client.dll:405741564883EC68488BF9E8????????4C8BF04885C0" };
    const ::addresses::entry weapon_update_composite_material{ "weapon_update_composite_material", "client.dll:48895C241048896C2418488974242057415641574883EC20440FB6F2488BF9" };
    const ::addresses::entry weapon_update_mesh{ "weapon_update_mesh", "client.dll:405556574154415541564157B8F01000" };
    const ::addresses::entry weapon_update_skin{ "weapon_update_skin", "client.dll:488D8B????????B201E8????????33D2488BCB>E8" };
    const ::addresses::entry econ_item_view_set_attribute{ "econ_item_view_set_attribute", "client.dll:40534883EC20488BD94881C108020000" };
    const ::addresses::entry econ_item_view_remove_attribute{ "econ_item_view_remove_attribute", "client.dll:40534883EC20486381????????440FB7CA" };
    const ::addresses::entry econ_item_view_invalidate_description{ "econ_item_view_invalidate_description", "client.dll:48895C24??48897424??574883EC20488DB9????????488BF1" };
    const ::addresses::entry set_bodygroup{ "set_bodygroup", "client.dll:85D20F88????????555657" };
    const ::addresses::entry anim_graph_rebuild{ "anim_graph_rebuild", "client.dll:4055564883EC284C89742458488BF180FAFF75??0FB65118" };
    const ::addresses::entry base_fire_guns_get_inaccuracy_alt{ "base_fire_guns_get_inaccuracy_alt", "client.dll:488BC45541554157488DA828FFFFFF4881ECE00100000F2970C84C8BF9" };
    const ::addresses::entry fire_event_client_side{ "fire_event_client_side", "client.dll:40535641544883EC30488BF2" };
    const ::addresses::entry game_event_get_name{ "game_event_get_name", "client.dll:8B41140FBAE01E7305488D4118C3" };
    const ::addresses::entry get_resource_view{ "get_resource_view", "rendersystemdx11.dll:48895C2410488974241848897C242048894C2408554154415541564157488D6C24E04881EC2001000033FF4D0FBEF8897D50450FB6E14C8B2D????????488BDA4885D20F84????????8B422085C07E??4C8B324D85F675??4885D20F84????????488D35????????48897D50488BD6488D4D50FF15????????488B4308488D4C2440897C244048897C2448C7442444C80000C048897D1048897D184885C00F85????????E8????????E9????????4180FFFF" };
    const ::addresses::entry match_found_handler{ "match_found_handler", "client.dll:4885D20F84????????488BC455535657488DA8????????4881EC????????33F6488BFA488BD9483B91" };
    const ::addresses::entry panorama_event{ "panorama_event", "client.dll:40565741574883EC40488B3D????????" };
    const ::addresses::entry particle_draw_array{ "particle_draw_array", "particles.dll:48895C24??4C894C24??4C894424??55" };
    const ::addresses::entry prediction_finish_move{ "prediction_finish_move", "client.dll:4D8BC6498BD5488BCF>E8????????488B8798010000" };
    const ::addresses::entry draw_smoke_array{ "draw_smoke_array", "client.dll:48895C24??48896C24??48897424??57415641574883EC??488B9C24????????4D8BF8488BFA488BF1" };
    const ::addresses::entry draw_smoke_array_fallback{ "draw_smoke_array_fallback", "client.dll:4D8BF8488BFA488BF14533C0BA????????458BF1488D8B????????E8-20" };
    const ::addresses::entry smoke_volume_list{ "smoke_volume_list", "client.dll:488B0D*????????0FB715????????4D896308" };
    const ::addresses::entry smoke_volume_list_head{ "smoke_volume_list_head", "client.dll:0FB705*????????33F64D896B10" };
    const ::addresses::entry set_player_ready{ "set_player_ready", "client.dll:40534883EC20488BDA488D15????????488BCBFF" };
    const ::addresses::entry sys_session_client_dispatch{ "sys_session_client_dispatch", "matchmaking.dll:48895C241048896C2418565741564883EC60488BD9498BF1488BCA450FB6F0488BFAFF15????????" };
    const ::addresses::entry vote_start_handler{ "vote_start_handler", "client.dll:48895C24??48896C24??564883EC408B5A60488BF2488BE983FBFF" };
    const ::addresses::entry vac_integrity_vtable{ "vac_integrity_vtable", "client.dll:4053415541574883EC??4889AC24" };
    const ::addresses::entry vac_integrity_module_crc{ "vac_integrity_module_crc", "client.dll:488BC4554156488D68??4881EC????????488958??0F57C0" };
    const ::addresses::entry vac_thread_report{ "vac_thread_report", "client.dll:48895C24??48896C24??48897424??48897C24??41564881EC????????8BEA" };
    const ::addresses::entry vac_send_telemetry_164{ "vac_send_telemetry_164", "client.dll:48895C2408488974241048897C24184C896424205541564157488D6C24B94881EC????????448BF94963D9488B0D????????" };
    const ::addresses::entry vac_field_ret_tracker{ "vac_field_ret_tracker", "client.dll:48895C240848896C2410488974241848897C242041564883EC??418BF14863F9" };
    const ::addresses::entry vac_field_monitor{ "vac_field_monitor", "client.dll:405341574883EC??4889742458488BF148897C2460498BF84C897424304C63F2" };
    const ::addresses::entry vac_thread_probe{ "vac_thread_probe", "client.dll:48894C24084881EC????????48C7442438????????48C7442430????????FF15????????" };

    const ::addresses::entry merge_subtick{ "merge_subtick", "client.dll:8954241048894C24085356574883EC704863DA488DB9280200004869C328090000" };
    const ::addresses::entry get_user_cmd_legacy{ "get_user_cmd_legacy", "client.dll:48895C2408574883EC208BFAE8????????488BD8448BC7B8" };
    const ::addresses::entry camera_think{ "camera_think", "client.dll:40555356574157488DAC2470FEFFFF4881EC900200004863DA488BF94869F328090000" };
    const ::addresses::entry get_hitbox_set{ "get_hitbox_set", "client.dll:48895C24??48897424??574881EC????????8BDA488BF9E8????????488BF04885C00F84" };
    const ::addresses::entry get_bone_index_for_hitbox{ "get_bone_index_for_hitbox", "client.dll:48895C241048896C2418564883EC504963E8488BF14C8BC239A9" };
    const ::addresses::entry skeleton_bone_array{ "skeleton_bone_array", "client.dll:488B97????????498BCE4C63C349C1E005" };
    const ::addresses::entry spread_seed{ "spread_seed", "client.dll:48895C24??574881EC????????F30F100A" };
    const ::addresses::entry calc_spread{ "calc_spread", "client.dll:488BC4488958184889682089501056574154415641574881EC????????448B15" };
    const ::addresses::entry trace_shape{ "trace_shape", "client.dll:488954241048894C240855535657415441564157488DAC24????????B8????????E8????????482BE0" };
    const ::addresses::entry game_trace_manager_legacy{ "game_trace_manager_legacy", "client.dll:4C8B3D*????????24C90C49660F7F45" };
    const ::addresses::entry set_view_angles_full{ "set_view_angles_full", "client.dll:85D2753D486381500B0000F2410F1000" };
    const ::addresses::entry entity_list_legacy{ "entity_list_legacy", "client.dll:488B0D*????????4885C974??4183F8FE" };
    const ::addresses::entry view_matrix_fn{ "view_matrix_fn", "client.dll:4863C1488D0D????????48C1E0064803C1C3" };
    const ::addresses::entry get_aim_punch_fn{ "get_aim_punch_fn", "client.dll:488BC4488958104889681848897020574883EC70488BEA410FB6F0" };
    const ::addresses::entry weapon_composite_offset{ "weapon_composite_offset", "client.dll:488D8B????????B201E8????????33D2488BCBE8" };

    namespace
    {
        const ::addresses::entry* const table[] = {
            &add_entity,
            &base_fire_guns_get_inaccuracy,
            &button_state_alloc,
            &cmd_interpreter,
            &create_move,
            &csgo_input,
            &draw_flash_effect,
            &draw_legs,
            &draw_overhead,
            &draw_scene_object,
            &draw_scene_object_array,
            &draw_skybox_array,
            &dynamic_light_alloc,
            &dynamic_light_manager,
            &dynamic_light_time,
            &engine_client_cmd,
            &entity_list,
            &filesystem_close,
            &find_hud_element,
            &frame_input_ring_base,
            &frame_input_ring_idx,
            &frame_stage_notify,
            &game_entity_system,
            &game_event_get_controller,
            &game_event_get_float,
            &game_event_get_int,
            &game_event_get_pawn,
            &game_event_get_string,
            &game_event_manager,
            &game_rules,
            &game_scene_node_set_mesh_group,
            &game_scene_node_set_skeleton,
            &game_trace_manager,
            &generate_primitives,
            &get_aim_punch,
            &get_bone_index,
            &get_glow_color,
            &get_inaccuracy,
            &get_interp_amount,
            &get_interpolated_shoot_position,
            &get_spread,
            &get_net_channel,
            &get_tick_view_angles,
            &get_transforms_for_hitbox_list,
            &get_usercmd,
            &get_usercmd_base,
            &get_view_angles,
            &get_world_group_handle,
            &get_world_group_id,
            &global_vars,
            &handle_view_angles,
            &history_field_alloc,
            &hud,
            &hud_death_notice_clear,
            &hud_weapon_selection_update,
            &init_particle_path_buffer,
            &init_particle_path_buffer_alt,
            &is_glowing,
            &item_system,
            &kv3_alloc,
            &kv3_destroy,
            &kv3_load,
            &level_initialization,
            &level_shutdown,
            &light_data_queue,
            &light_scene_object,
            &local_player_controller,
            &log_internal,
            &material_create,
            &material_manager,
            &override_view,
            &parse_report_hit,
            &particle_create_effect,
            &particle_destroy_effect,
            &particle_manager,
            &particle_set_control_point,
            &particle_set_entity_binding,
            &particle_set_transform,
            &planted_c4,
            &post_network_data_received,
            &prediction_player,
            &prediction_process_movement,
            &prediction_reset_pawn,
            &prediction_seed,
            &prediction_set_pawn,
            &prediction_set_state,
            &prediction_setup_move,
            &prediction_state,
            &prepare_scene_material,
            &process_input_event,
            &read_frame_input,
            &remove_entity,
            &render_crosshair,
            &render_decals,
            &render_game_system_storage,
            &render_scope,
            &render_smoke,
            &render_view,
            &resource_system_load,
            &resource_system_precache,
            &serialize_move_crc,
            &service_read,
            &set_info,
            &set_player_model,
            &set_postprocess_vec,
            &set_shader_param,
            &set_shader_param_i,
            &set_view_angles,
            &set_voice_data,
            &simulation_player,
            &sort_primitives,
            &setup_fog,
            &play_sound,
            &string_copy,
            &subtick_move_alloc,
            &trace_bullet,
            &trace_bullet_data_init,
            &trace_bullet_free,
            &trace_bullet_update,
            &trace_filter_init,
            &trace_filter_set_collision,
            &trace_hull,
            &trace_ray,
            &trace_ray_entity,
            &update_fov_sensitivity,
            &utl_vector_push,
            &view_matrix,
            &viewmodel_update_mesh,
            &weapon_calculate_spread,
            &weapon_get_entity_index,
            &weapon_get_model_path,
            &weapon_get_recoil_offset,
            &weapon_get_viewmodel,
            &weapon_recoil_data,
            &weapon_set_mesh_group_mask,
            &weapon_update_accuracy,
            &weapon_update_composite_material,
            &weapon_update_mesh,
            &weapon_update_skin,
            &econ_item_view_set_attribute,
            &econ_item_view_remove_attribute,
            &econ_item_view_invalidate_description,
            &set_bodygroup,
            &anim_graph_rebuild,
            &base_fire_guns_get_inaccuracy_alt,
            &fire_event_client_side,
            &game_event_get_name,
            &get_resource_view,
            &match_found_handler,
            &panorama_event,
            &particle_draw_array,
            &prediction_finish_move,
            &draw_smoke_array,
            &draw_smoke_array_fallback,
            &smoke_volume_list,
            &smoke_volume_list_head,
            &set_player_ready,
            &sys_session_client_dispatch,
            &vote_start_handler,
            &vac_integrity_vtable,
            &vac_integrity_module_crc,
            &vac_thread_report,
            &vac_send_telemetry_164,
            &vac_field_ret_tracker,
            &vac_field_monitor,
            &vac_thread_probe,
            &merge_subtick,
            &get_user_cmd_legacy,
            &camera_think,
            &get_hitbox_set,
            &get_bone_index_for_hitbox,
            &skeleton_bone_array,
            &spread_seed,
            &calc_spread,
            &trace_shape,
            &game_trace_manager_legacy,
            &set_view_angles_full,
            &entity_list_legacy,
            &view_matrix_fn,
            &get_aim_punch_fn,
            &weapon_composite_offset,
        };
    }

    std::span<const ::addresses::entry* const> all()
    {
        return table;
    }
}
