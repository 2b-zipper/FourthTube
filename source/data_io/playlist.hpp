#pragma once
#include <vector>
#include <string>
#include <time.h>

struct PlaylistVideo {
    std::string id;
    std::string title;
    std::vector<std::string> title_lines;
    std::string author_name;
    std::string length_text;
    int my_view_count;
    time_t last_watch_time;
    bool valid = true;
};

class Playlist {
public:
    std::string name;
    std::vector<PlaylistVideo> videos;
    std::vector<PlaylistVideo> queue;
    bool continuous_playback = false;

    void add_video(const PlaylistVideo &video);
    void remove_video(const std::string &video_id);
    void add_to_queue(const PlaylistVideo &video);
    void remove_from_queue(const std::string &video_id);
    PlaylistVideo get_next_video();
    std::vector<PlaylistVideo> get_all_videos();
    std::vector<PlaylistVideo> get_queue();
    void clear_queue();
    void set_continuous_playback(bool enable);
    bool is_continuous_playback_enabled();
};

void load_playlist();
void save_playlist();
void add_video_to_playlist(PlaylistVideo video);
void remove_video_from_playlist(const std::string &video_id);
std::vector<PlaylistVideo> get_all_videos();
std::vector<PlaylistVideo> get_queue();
PlaylistVideo get_next_video();
void clear_queue();
void set_continuous_playback(bool enable);
bool is_continuous_playback_enabled();