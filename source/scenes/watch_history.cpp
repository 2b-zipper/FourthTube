#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/watch_history.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "data_io/history.hpp"
#include "data_io/history_sync.hpp"
#include "util/misc_tasks.hpp"
#include "util/async_task.hpp"
#include "oauth/oauth.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12

struct ContainerView : public FixedSizeView {
	std::vector<View *> views;

	ContainerView(double x0, double y0, double width, double height)
	    : View(x0, y0), FixedSizeView(x0, y0, width, height) {}

	ContainerView *set_views(std::vector<View *> views) {
		this->views = views;
		return this;
	}

	void draw_() const override {
		for (auto *v : views) {
			v->draw(x0, y0);
		}
	}

	void update_(Hid_info key) override {
		for (auto *v : views) {
			v->update(key, x0, y0);
		}
	}

	void on_scroll() override {
		View::on_scroll();
		for (auto *v : views) {
			v->on_scroll();
		}
	}

	void recursive_delete_subviews() override {
		for (auto *v : views) {
			v->recursive_delete_subviews();
			delete v;
		}
		views.clear();
	}
};

struct CustomScrollView : public ScrollView {
	CustomScrollView(double x0, double y0, double width, double height)
	    : View(x0, y0), ScrollView(x0, y0, width, height) {}

	void draw_() const override {
		if (offset < 0) {
			auto *mut_this = const_cast<CustomScrollView *>(this);
			double shift = std::min(21.0, std::max(0.0, -(double)offset));
			mut_this->y0 += shift;
			mut_this->y1 += shift;
			double y_offset = y0 - offset;
			for (int i = 0; i < (int)views.size(); i++) {
				auto &view = views[i];
				double cur_height = view->get_height();
				if (y_offset < y1 && y_offset + cur_height > 0) {
					view->draw(x0, y_offset);
					for (auto &callback : on_child_drawn_callbacks) {
						if (callback.first == i) {
							callback.second(*this, i);
						}
					}
				}
				y_offset += cur_height + margin;
			}
			mut_this->y0 -= shift;
			mut_this->y1 -= shift;
		} else {
			double y_offset = y0 - offset;
			for (int i = 0; i < (int)views.size(); i++) {
				auto &view = views[i];
				double cur_height = view->get_height();
				if (y_offset < y1 && y_offset + cur_height > 0) {
					view->draw(x0, y_offset);
					for (auto &callback : on_child_drawn_callbacks) {
						if (callback.first == i) {
							callback.second(*this, i);
						}
					}
				}
				y_offset += cur_height + margin;
			}
		}
	}

	void draw_slider_bar() const {
		float displayed_height = y1 - y0;
		if (content_height > displayed_height) {
			float bar_len = displayed_height * displayed_height / content_height;
			float bar_pos = (float)offset / content_height * displayed_height;
			Draw_texture(var_square_image[0], DEF_DRAW_GRAY, x1 - 3, y0 + bar_pos, 3, bar_len);
		}

		if (pull_to_refresh_enabled && pull_refresh_loading) {
			u32 indicator_alpha = 200;
			float center_x = x0 + (x1 - x0) / 2;
			float center_y = y0 + 31;
			float size = 14;
			int segments = 8;
			float segment_angle = 360.0f / segments;

			for (int i = 0; i < segments; i++) {
				float angle = (i * segment_angle + pull_refresh_rotation) * 3.14159f / 180.0f;
				float radius = size * 0.6f;
				float seg_x = center_x + cos(angle) * radius;
				float seg_y = center_y + sin(angle) * radius;

				u32 seg_alpha = (u32)((indicator_alpha * (segments - i % 4)) / segments);
				u32 seg_color = (seg_alpha << 24) | DEF_DRAW_WEAK_GREEN;

				Draw_texture(var_square_image[0], seg_color, seg_x - 1.5f, seg_y - 1.5f, 3, 3);
			}
		}
	}
};

struct ScrollBarOverlayView : public View {
	CustomScrollView *target;
	ScrollBarOverlayView(CustomScrollView *target) : View(0, 0), target(target) {}
	float get_width() const override { return 0; }
	float get_height() const override { return 0; }
	void draw_() const override { target->draw_slider_bar(); }
	void update_(Hid_info key) override {}
};

namespace WatchHistory {
bool thread_suspend = false;
bool already_init = false;
bool exiting = false;

Mutex resource_lock;

std::vector<HistoryVideo> watch_history;
std::vector<HistoryVideo> oauth_watch_history;
std::string clicked_url;
std::string erase_request;

int cur_sort_type = 0;
int sort_request = -1;

static bool last_oauth_state = false;
static bool last_disable_pull_to_refresh = false;

int CONTENT_Y_HIGHT = 240; // changes according to whether the video playing bar is drawn or not

OverlayView *on_long_tap_dialog;
View *main_view = NULL;
ScrollView *local_history_tab_view = NULL;
VerticalListView *local_video_list_view = NULL;
View *oauth_history_tab_view = NULL;
ScrollView *oauth_history_scroll_view = NULL;
VerticalListView *oauth_video_list_view = NULL;
ContainerView *oauth_history_header_container = NULL;

std::string oauth_history_continuation_token = "";
bool oauth_history_has_more = false;
bool oauth_history_loading = false;
bool oauth_history_loaded = false;
}; // namespace WatchHistory
using namespace WatchHistory;

static void update_watch_history(const std::vector<HistoryVideo> &new_watch_history);
static void update_oauth_watch_history(const std::vector<HistoryVideo> &new_oauth_history);
static void append_oauth_watch_history_views(const std::vector<HistoryVideo> &new_items);
static void load_oauth_watch_history(void *);
static void load_oauth_watch_history_more(void *);
static void rebuild_history_tabs();

static void update_watch_history(const std::vector<HistoryVideo> &new_watch_history);

void History_init(void) {
	logger.info("history/init", "Initializing...");

	on_long_tap_dialog = new OverlayView(0, 0, 320, 240);
	on_long_tap_dialog->set_is_visible(false);

	load_watch_history();

	local_video_list_view = (new VerticalListView(0, 0, 320))
	                            ->set_margin(SMALL_MARGIN)
	                            ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HISTORY);
	oauth_video_list_view = (new VerticalListView(0, 0, 320))
	                            ->set_margin(SMALL_MARGIN)
	                            ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HISTORY);

	update_watch_history(get_valid_watch_history());

	rebuild_history_tabs();

	last_oauth_state = OAuth::is_authenticated();

	History_resume("");
	already_init = true;
}
void History_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;

	logger.info("history/exit", "Exited.");
}
void History_suspend(void) { thread_suspend = true; }
void History_resume(std::string arg) {
	(void)arg;

	if (local_history_tab_view) {
		local_history_tab_view->on_resume();
	}
	if (oauth_history_scroll_view) {
		oauth_history_scroll_view->on_resume();
	}
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_refresh = true;

	bool current_oauth_state = OAuth::is_authenticated();
	bool current_disable_pull_to_refresh = var_disable_pull_to_refresh;
	if (current_oauth_state != last_oauth_state || current_disable_pull_to_refresh != last_disable_pull_to_refresh) {
		resource_lock.lock();
		update_watch_history(get_valid_watch_history());
		resource_lock.unlock();

		rebuild_history_tabs();

		if (current_oauth_state && current_oauth_state != last_oauth_state &&
		    !is_async_task_running(load_oauth_watch_history)) {
			queue_async_task(load_oauth_watch_history, NULL);
		}

		last_oauth_state = current_oauth_state;
		last_disable_pull_to_refresh = current_disable_pull_to_refresh;
	} else {
		resource_lock.lock();
		update_watch_history(get_valid_watch_history());
		resource_lock.unlock();
	}
}

static void update_watch_history(const std::vector<HistoryVideo> &new_watch_history) {
	watch_history = new_watch_history;

	std::vector<View *> new_views;
	for (auto i : watch_history) {
		std::string view_count_str;
		{
			std::string view_count_str_tmp = LOCALIZED(MY_VIEW_COUNT_WITH_NUMBER);
			for (size_t j = 0; j < view_count_str_tmp.size();) {
				if (j + 1 < view_count_str_tmp.size() && view_count_str_tmp[j] == '%' &&
				    view_count_str_tmp[j + 1] == '0') {
					view_count_str += std::to_string(i.my_view_count), j += 2;
				} else {
					view_count_str.push_back(view_count_str_tmp[j]), j++;
				}
			}
		}
		std::string last_watch_time_str;
		{
			char tmp[100];
			strftime(tmp, 100, "%Y/%m/%d %H:%M", gmtime(&i.last_watch_time));
			last_watch_time_str = tmp;
		}

		SuccinctVideoView *cur_view =
		    (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
		        ->set_title_lines(i.title_lines)
		        ->set_auxiliary_lines({i.author_name, view_count_str + " " + last_watch_time_str})
		        ->set_bottom_right_overlay(i.length_text)
		        ->set_thumbnail_url(youtube_get_video_thumbnail_url_by_id(i.id));

		cur_view
		    ->set_get_background_color([](const View &view) {
			    int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
			    if (var_night_mode) {
				    darkness = 0xFF - darkness;
			    }
			    return COLOR_GRAY(darkness);
		    })
		    ->set_on_view_released([i](View &view) { clicked_url = youtube_get_video_url_by_id(i.id); })
		    ->add_on_long_hold(40, [i](View &view) {
			    on_long_tap_dialog->recursive_delete_subviews();
			    on_long_tap_dialog
			        ->set_subview(
			            (new TextView(0, 0, 180, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
			                ->set_text((std::function<std::string()>)[]() { return LOCALIZED(REMOVE_HISTORY_ITEM); })
			                ->set_x_alignment(TextView::XAlign::CENTER)
			                ->set_y_alignment(TextView::YAlign::CENTER)
			                ->set_text_offset(0, -1)
			                ->set_on_view_released([i](View &view) {
				                erase_request = i.id;
				                if (local_history_tab_view) {
					                local_history_tab_view->reset_holding_status();
				                }
				                on_long_tap_dialog->set_is_visible(false);
				                var_need_refresh = true;
			                })
			                ->set_get_background_color([](const View &view) {
				                int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
				                if (var_night_mode) {
					                darkness = 0xFF - darkness;
				                }
				                return COLOR_GRAY(darkness);
			                }))
			        ->set_on_cancel([](OverlayView &view) {
				        if (local_history_tab_view) {
					        local_history_tab_view->reset_holding_status();
				        }
				        view.set_is_visible(false);
				        var_need_refresh = true;
			        })
			        ->set_is_visible(true);
			    var_need_refresh = true;
		    });

		new_views.push_back(cur_view);
	}

	if (local_video_list_view) {
		local_video_list_view->recursive_delete_subviews();
		local_video_list_view->set_views(new_views);
	}
}

static void update_oauth_watch_history(const std::vector<HistoryVideo> &new_oauth_history) {
	oauth_watch_history = new_oauth_history;
	std::vector<View *> new_views;
	new_views.push_back(new EmptyView(0, 0, 320, 21 - SMALL_MARGIN));

	for (auto i : oauth_watch_history) {
		std::string last_watch_time_str;
		{
			char tmp[100];
			strftime(tmp, 100, "%Y/%m/%d %H:%M", gmtime(&i.last_watch_time));
			last_watch_time_str = tmp;
		}

		SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
		                                  ->set_title_lines(i.title_lines)
		                                  ->set_auxiliary_lines({i.author_name, i.view_count_text})
		                                  ->set_bottom_right_overlay(i.length_text)
		                                  ->set_thumbnail_url(youtube_get_video_thumbnail_url_by_id(i.id));

		cur_view
		    ->set_get_background_color([](const View &view) {
			    int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
			    if (var_night_mode) {
				    darkness = 0xFF - darkness;
			    }
			    return COLOR_GRAY(darkness);
		    })
		    ->set_on_view_released([i](View &view) { clicked_url = youtube_get_video_url_by_id(i.id); });

		new_views.push_back(cur_view);
	}

	if (oauth_video_list_view) {
		oauth_video_list_view->swap_views(new_views);
	}
}

static void append_oauth_watch_history_views(const std::vector<HistoryVideo> &new_items) {
	std::vector<View *> new_views;
	for (auto i : new_items) {
		SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT))
		                                  ->set_title_lines(i.title_lines)
		                                  ->set_auxiliary_lines({i.author_name, i.view_count_text})
		                                  ->set_bottom_right_overlay(i.length_text)
		                                  ->set_thumbnail_url(youtube_get_video_thumbnail_url_by_id(i.id));

		cur_view
		    ->set_get_background_color([](const View &view) {
			    int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
			    if (var_night_mode) {
				    darkness = 0xFF - darkness;
			    }
			    return COLOR_GRAY(darkness);
		    })
		    ->set_on_view_released([i](View &view) { clicked_url = youtube_get_video_url_by_id(i.id); });

		new_views.push_back(cur_view);
	}

	if (oauth_video_list_view) {
		oauth_video_list_view->views.insert(oauth_video_list_view->views.end(), new_views.begin(), new_views.end());
	}
}

static void load_oauth_watch_history(void *) {
	if (!OAuth::is_authenticated()) {
		return;
	}

	resource_lock.lock();
	if (oauth_history_loading) {
		resource_lock.unlock();
		return;
	}
	oauth_history_loading = true;
	resource_lock.unlock();

	std::string next_token;
	auto new_oauth_history = get_oauth_watch_history("", &next_token);

	resource_lock.lock();
	oauth_watch_history = new_oauth_history;
	oauth_history_continuation_token = next_token;
	oauth_history_has_more = !next_token.empty();

	update_oauth_watch_history(new_oauth_history);

	if (oauth_history_scroll_view) {
		oauth_history_scroll_view->finish_pull_refresh();
	}

	oauth_history_loading = false;
	var_need_refresh = true;
	resource_lock.unlock();
}

static void load_oauth_watch_history_more(void *) {
	if (!OAuth::is_authenticated()) {
		return;
	}

	resource_lock.lock();
	if (oauth_history_loading || !oauth_history_has_more) {
		resource_lock.unlock();
		return;
	}
	oauth_history_loading = true;
	std::string current_token = oauth_history_continuation_token;
	resource_lock.unlock();

	std::string next_token;
	auto new_items = get_oauth_watch_history(current_token, &next_token);

	resource_lock.lock();
	if (exiting) {
		oauth_history_loading = false;
		resource_lock.unlock();
		return;
	}

	if (!new_items.empty()) {
		oauth_watch_history.insert(oauth_watch_history.end(), new_items.begin(), new_items.end());
		append_oauth_watch_history_views(new_items);
	}

	oauth_history_continuation_token = next_token;
	oauth_history_has_more = !next_token.empty();
	oauth_history_loading = false;
	var_need_refresh = true;
	resource_lock.unlock();
}

static ScrollView *create_local_history_tab() {
	constexpr int selector_width = 180;

	ScrollView *tab =
	    (new ScrollView(0, 0, 320, 240))
	        ->set_views(
	            {(new HorizontalListView(0, 0, MIDDLE_FONT_INTERVAL))
	                 ->set_views(
	                     {(new TextView(0, 0, 320 - selector_width, MIDDLE_FONT_INTERVAL))
	                          ->set_text((std::function<std::string()>)[]() { return LOCALIZED(WATCH_HISTORY); })
	                          ->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL),
	                      (new SelectorView(0, 0, selector_width, MIDDLE_FONT_INTERVAL, false))
	                          ->set_texts({(std::function<std::string()>)[]() { return LOCALIZED(BY_LAST_WATCH_TIME); },
	                                       (std::function<std::string()>)[]() { return LOCALIZED(BY_MY_VIEW_COUNT); }},
	                                      cur_sort_type)
	                          ->set_on_change([](const SelectorView &view) {
		                          sort_request = cur_sort_type = view.selected_button;
	                          })}),
	             (new RuleView(0, 0, 320, 3)), local_video_list_view});

	return tab;
}

static View *create_reload_banner() {
	return (new VerticalListView(0, 0, 320))
	    ->set_views({(new TextView(0, 0, 320, 18))
	                     ->set_text((std::function<std::string()>)[]() {
		                     auto res = LOCALIZED(RELOAD);
		                     if (is_async_task_running(load_oauth_watch_history)) {
			                     res += " ...";
		                     }
		                     return res;
	                     })
	                     ->set_text_offset(SMALL_MARGIN, -1)
	                     ->set_on_view_released([](View &) {
		                     if (!is_async_task_running(load_oauth_watch_history)) {
			                     queue_async_task(load_oauth_watch_history, NULL);
		                     }
	                     })
	                     ->set_get_background_color([](const View &view) -> u32 {
		                     if (is_async_task_running(load_oauth_watch_history)) {
			                     return LIGHT0_BACK_COLOR;
		                     }
		                     return View::STANDARD_BACKGROUND(view);
	                     }),
	                 (new RuleView(0, 0, 320, SMALL_MARGIN))->set_margin(0)->set_get_background_color([](const View &) {
		                 return DEFAULT_BACK_COLOR;
	                 })})
	    ->set_draw_order({1, 0});
}

static void create_oauth_history_tab() {
	TextView *header_text = (new TextView(0, 0, 320, 18))
	                            ->set_text((std::function<std::string()>)[]() { return LOCALIZED(WATCH_HISTORY); })
	                            ->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL);

	header_text->set_get_background_color([](const View &view) {
		if (var_night_mode) {
			return 0xFF000000;
		}
		return 0xFFFFFFFF;
	});

	oauth_history_header_container =
	    (new ContainerView(0, 0, 320, 21))->set_views({header_text, (new RuleView(0, 18, 320, 3))});

	if (var_disable_pull_to_refresh) {
		oauth_history_scroll_view = new CustomScrollView(0, 21, 320, 240);
		oauth_history_scroll_view->set_views({oauth_video_list_view});
		oauth_history_scroll_view->set_pull_to_refresh(false, []() {});

		oauth_history_tab_view =
		    (new ContainerView(0, 0, 320, 240))
		        ->set_views({oauth_history_scroll_view, create_reload_banner(),
		                     new ScrollBarOverlayView((CustomScrollView *)oauth_history_scroll_view)});
	} else {
		oauth_history_scroll_view =
		    (new CustomScrollView(0, 0, 320, 240))->set_views({oauth_video_list_view})->set_pull_to_refresh(true, []() {
			    queue_async_task(load_oauth_watch_history, NULL);
		    });

		oauth_history_tab_view =
		    (new ContainerView(0, 0, 320, 240))
		        ->set_views({oauth_history_scroll_view, oauth_history_header_container,
		                     new ScrollBarOverlayView((CustomScrollView *)oauth_history_scroll_view)});
	}
}

static void rebuild_history_tabs() {
	resource_lock.lock();

	if (main_view) {
		delete main_view;
		main_view = NULL;
	}
	if (!local_video_list_view) {
		local_video_list_view = (new VerticalListView(0, 0, 320))
		                            ->set_margin(SMALL_MARGIN)
		                            ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HISTORY);
	}
	if (!oauth_video_list_view) {
		oauth_video_list_view = (new VerticalListView(0, 0, 320))
		                            ->set_margin(SMALL_MARGIN)
		                            ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HISTORY);
	}

	local_history_tab_view = create_local_history_tab();
	create_oauth_history_tab();

	if (OAuth::is_authenticated()) {
		main_view = (new TabView(0, 0, 320, 0))
		                ->set_views({local_history_tab_view, oauth_history_tab_view})
		                ->set_tab_texts<std::function<std::string()>>(
		                    {[]() { return LOCALIZED(LOCAL_CHANNELS); }, []() { return LOCALIZED(ACCOUNT); }})
		                ->set_lr_tab_switch_enabled(false);
	} else {
		main_view = local_history_tab_view;
	}

	resource_lock.unlock();
	var_need_refresh = true;
}

void History_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);

	thumbnail_set_active_scene(SceneType::HISTORY);

	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGHT = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;

	if (oauth_history_scroll_view && oauth_history_header_container && !var_disable_pull_to_refresh) {
		double offset = oauth_history_scroll_view->get_offset();
		double header_y = std::min(0.0, -offset);
		oauth_history_header_container->update_y_range(header_y, header_y + 21);
	}

	if (oauth_history_scroll_view && OAuth::is_authenticated() && oauth_history_has_more && !oauth_history_loading) {
		if (oauth_history_scroll_view->get_offset() >= oauth_video_list_view->get_height() -
		                                                   oauth_history_scroll_view->get_height() -
		                                                   VIDEO_LIST_THUMBNAIL_HEIGHT * 2) {
			if (!is_async_task_running(load_oauth_watch_history_more)) {
				queue_async_task(load_oauth_watch_history_more, NULL);
			}
		}
	}

	TabView *main_tab_view = dynamic_cast<TabView *>(main_view);
	if (main_tab_view) {
		main_tab_view->update_y_range(0, CONTENT_Y_HIGHT);
		local_history_tab_view->update_y_range(0, CONTENT_Y_HIGHT - main_tab_view->tab_selector_height);
		if (oauth_history_scroll_view) {
			oauth_history_scroll_view->update_y_range(0, CONTENT_Y_HIGHT - main_tab_view->tab_selector_height);
		}
	} else if (main_view) {
		ScrollView *main_scroll_view = dynamic_cast<ScrollView *>(main_view);
		if (main_scroll_view) {
			main_scroll_view->update_y_range(0, CONTENT_Y_HIGHT);
		}
	}

	TabView *tab_view_ptr = dynamic_cast<TabView *>(main_view);
	if (tab_view_ptr && tab_view_ptr->selected_tab == 1 && OAuth::is_authenticated() && !oauth_history_loaded &&
	    !oauth_history_loading) {
		oauth_history_loaded = true;
		if (!is_async_task_running(load_oauth_watch_history)) {
			queue_async_task(load_oauth_watch_history, NULL);
		}
	}

	if (var_need_refresh || !var_eco_mode) {
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();

		Draw_screen_ready(1, DEFAULT_BACK_COLOR);

		resource_lock.lock();
		if (main_view) {
			main_view->draw();
		}
		resource_lock.unlock();
		on_long_tap_dialog->draw();

		if (video_playing_bar_show) {
			video_draw_playing_bar();
		}
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT
		                                         : 240 - OVERLAY_MENU_ICON_SIZE);

		if (Util_expl_query_show_flag()) {
			Util_expl_draw();
		}

		if (Util_err_query_error_show_flag()) {
			Util_err_draw();
		}

		Draw_touch_pos();

		Draw_apply_draw();
	} else {
		gspWaitForVBlank();
	}

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if (Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		if (on_long_tap_dialog->is_visible) {
			on_long_tap_dialog->update(key);
		} else {
			update_overlay_menu(&key);

			resource_lock.lock();
			if (main_view) {
				main_view->update(key);
			}
			if (clicked_url != "") {
				global_intent.next_scene = SceneType::VIDEO_PLAYER;
				global_intent.arg = clicked_url;
				clicked_url = "";
			}
			if (sort_request != -1) {
				auto tmp_watch_history = watch_history;
				std::sort(tmp_watch_history.begin(), tmp_watch_history.end(),
				          [](const HistoryVideo &i, const HistoryVideo &j) {
					          if (sort_request == 0) {
						          return i.last_watch_time > j.last_watch_time;
					          }
					          if (sort_request == 1) {
						          return i.my_view_count > j.my_view_count;
					          }
					          return false;
				          });
				update_watch_history(tmp_watch_history);

				sort_request = -1;
			}
			if (erase_request != "") {
				std::vector<HistoryVideo> tmp_watch_history;
				for (auto video : watch_history) {
					if (video.id != erase_request) {
						tmp_watch_history.push_back(video);
					}
				}
				update_watch_history(tmp_watch_history);
				history_erase_by_id(erase_request);
				misc_tasks_request(TASK_SAVE_HISTORY);

				erase_request = "";
			}
			if (video_playing_bar_show) {
				video_update_playing_bar(key);
			}
			resource_lock.unlock();
		}

		if (key.p_b) {
			global_intent.next_scene = SceneType::BACK;
		}
	}
}
