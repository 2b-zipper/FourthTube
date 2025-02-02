#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "local_playlist.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "data_io/playlist.hpp"
#include "util/misc_tasks.hpp"
#include "scenes/channel.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12

namespace LocalPlaylist {
    bool thread_suspend = false;
    bool already_init = false;
    bool exiting = false;
    
    std::vector<PlaylistVideo> videos;
    std::string clicked_url;
    std::string erase_request;
    
    OverlayView *on_long_tap_dialog;
    ScrollView *main_view = NULL;
    VerticalListView *playlist_list_view = NULL;
    TextView *playlist_title_view = NULL;
    TextView *playlist_author_view = NULL;
    VerticalListView *playlist_list_videos_view = NULL;
    
    void LocalPlaylist_resume(std::string arg);
};
using namespace LocalPlaylist;

static void update_videos(const std::vector<PlaylistVideo> &new_videos);

void LocalPlaylist_init(void) {
    logger.info("local_playlist/init", "Initializing...");
    
    on_long_tap_dialog = new OverlayView(0, 0, 320, 240);
    on_long_tap_dialog->set_is_visible(false);
    
    load_playlist();

    already_init = true;
}

void LocalPlaylist_exit(void) {
    already_init = false;
    thread_suspend = false;
    exiting = true;
    
    logger.info("local_playlist/exit", "Exited.");
}

void LocalPlaylist_suspend(void) {
    thread_suspend = true;
}

void LocalPlaylist_resume(std::string arg) {
    (void) arg;
    
    if (main_view) main_view->on_resume();
    overlay_menu_on_resume();
    thread_suspend = false;
    var_need_refresh = true;
    
    update_videos(get_all_videos());
}

static void update_videos(const std::vector<PlaylistVideo> &new_videos) {
    videos = new_videos;
    
    if (main_view) main_view->recursive_delete_subviews();
    delete main_view;

    playlist_list_view = (new VerticalListView(0, 0, 320))
        ->set_margin(SMALL_MARGIN)
        ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::LOCAL_PLAYLIST);
    for (auto &video : videos) {
        SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
            ->set_title_lines(video.title_lines)
            ->set_auxiliary_lines({video.author_name, std::to_string(video.my_view_count) + " views"})
            ->set_thumbnail_url(youtube_get_video_thumbnail_url_by_id(video.id));
        
        cur_view->set_get_background_color([] (const View &view) {
            int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
            if (var_night_mode) darkness = 0xFF - darkness;
            return COLOR_GRAY(darkness);
        })->set_on_view_released([video] (View &view) {
            clicked_url = youtube_get_video_url_by_id(video.id);
        })->add_on_long_hold(40, [video] (View &view) {
            on_long_tap_dialog->recursive_delete_subviews();
            on_long_tap_dialog
                ->set_subview((new TextView(0, 0, 160, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
                    ->set_text((std::function<std::string ()>) [] () { return LOCALIZED(REMOVE_VIDEO); })
                    ->set_x_alignment(TextView::XAlign::CENTER)
                    ->set_y_alignment(TextView::YAlign::CENTER)
                    ->set_text_offset(0, -1)
                    ->set_on_view_released([video] (View &view) {
                        erase_request = video.id;
                        main_view->reset_holding_status();
                        on_long_tap_dialog->set_is_visible(false);
                        var_need_refresh = true;
                    })
                    ->set_get_background_color([] (const View &view) {
                        int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
                        if (var_night_mode) darkness = 0xFF - darkness;
                        return COLOR_GRAY(darkness);
                    })
                )
                ->set_on_cancel([] (OverlayView &view) {
                    main_view->reset_holding_status();
                    view.set_is_visible(false);
                    var_need_refresh = true;
                })
                ->set_is_visible(true);
            var_need_refresh = true;
        });
        
        playlist_list_view->views.push_back(cur_view);
    }
    main_view = (new ScrollView(0, 0, 320, 240))
        ->set_views({
            (new HorizontalListView(0, 0, MIDDLE_FONT_INTERVAL))
                ->set_views({
                    (new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
                        ->set_text((std::function<std::string()>) [] () { return LOCALIZED(PLAYLISTS); })
                        ->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL)
                }),
            (new RuleView(0, 0, 320, 3)),
            playlist_list_view
        });
}

void LocalPlaylist_draw(void) {
    Hid_info key;
    Util_hid_query_key_state(&key);
    
    thumbnail_set_active_scene(SceneType::LOCAL_PLAYLIST);
    
    bool video_playing_bar_show = video_is_playing();
    int CONTENT_Y_HIGHT = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
    main_view->update_y_range(0, CONTENT_Y_HIGHT);
    
    if(var_need_refresh || !var_eco_mode) {
        var_need_refresh = false;
        Draw_frame_ready();
        video_draw_top_screen();
        
        Draw_screen_ready(1, DEFAULT_BACK_COLOR);
        
        main_view->draw();
        on_long_tap_dialog->draw();
        
        if (video_playing_bar_show) video_draw_playing_bar();
        draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
        
        if(Util_expl_query_show_flag())
            Util_expl_draw();

        if(Util_err_query_error_show_flag())
            Util_err_draw();

        Draw_touch_pos();

        Draw_apply_draw();
    } else {
        gspWaitForVBlank();
    }
    
    if (Util_err_query_error_show_flag()) {
        Util_err_main(key);
    } else if(Util_expl_query_show_flag()) {
        Util_expl_main(key);
    } else {
        if (on_long_tap_dialog->is_visible) on_long_tap_dialog->update(key);
        else {
            update_overlay_menu(&key);
            
            main_view->update(key);
            if (clicked_url != "") {
                global_intent.next_scene = SceneType::VIDEO_PLAYER;
                global_intent.arg = clicked_url;
                clicked_url = "";
            }
            if (erase_request != "") {
                remove_video_from_playlist(erase_request);
                update_videos(get_all_videos());
                misc_tasks_request(TASK_SAVE_PLAYLIST);
                erase_request = "";
            }
            if (video_playing_bar_show) video_update_playing_bar(key);
        }
        
        if (key.p_b) global_intent.next_scene = SceneType::BACK;
    }
}