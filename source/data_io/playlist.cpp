#include "headers.hpp"
#include "playlist.hpp"
#include "util/util.hpp"
#include "ui/ui.hpp"
#include "rapidjson_wrapper.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>

using namespace rapidjson;

static Playlist playlist;
static Mutex resource_lock;

#define PLAYLIST_VERSION 0
#define PLAYLIST_DIR_PATH (DEF_MAIN_DIR + "Playlists/")
#define PLAYLIST_FILE_PATH (PLAYLIST_DIR_PATH + "local_playlist.json")

bool directory_exists(const std::string &path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return false;
    } else if (info.st_mode & S_IFDIR) {
        return true;
    } else {
        return false;
    }
}

void create_directory(const std::string &path) {
    mkdir(path.c_str(), 0777);
}

void load_playlist() {
    if (!directory_exists(PLAYLIST_DIR_PATH)) {
        create_directory(PLAYLIST_DIR_PATH);
    }

    std::string data;
    std::ifstream file(PLAYLIST_FILE_PATH);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        data = buffer.str();
        file.close();
    }

    Document json_root;
    std::string error;
    RJson data_json = RJson::parse(json_root, data.c_str(), error);

    int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
    if (version >= 0) {
        playlist.name = data_json["name"].string_value();
        playlist.continuous_playback = data_json["continuous_playback"].bool_value();
        for (auto video : data_json["videos"].array_items()) {
            PlaylistVideo cur_video;
            cur_video.id = video["id"].string_value();
            cur_video.title = video["title"].string_value();
            cur_video.title_lines = truncate_str(cur_video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);
            cur_video.author_name = video["author_name"].string_value();
            cur_video.length_text = video["length"].string_value();
            cur_video.my_view_count = video["my_view_count"].int_value();
            cur_video.last_watch_time = strtoll(video["last_watch_time"].string_value().c_str(), NULL, 10);
            cur_video.valid = youtube_is_valid_video_id(cur_video.id);
            if (!cur_video.valid) logger.caution("playlist/load", "invalid playlist item : " + cur_video.title);

            playlist.videos.push_back(cur_video);
        }
    } else {
        logger.error("playlist/load", "json err : " + data.substr(0, 40));
    }

    logger.info("playlist/load", "loaded playlist with " + std::to_string(playlist.videos.size()) + " items");
}

void save_playlist() {
    Document json_root;
    auto &allocator = json_root.GetAllocator();

    json_root.SetObject();
    json_root.AddMember("version", std::to_string(PLAYLIST_VERSION), allocator);
    json_root.AddMember("name", playlist.name, allocator);
    json_root.AddMember("continuous_playback", playlist.continuous_playback, allocator);

    Value video_array(kArrayType);
    for (auto video : playlist.videos) {
        Value cur_video(kObjectType);
        cur_video.AddMember("id", video.id, allocator);
        cur_video.AddMember("title", video.title, allocator);
        cur_video.AddMember("author_name", video.author_name, allocator);
        cur_video.AddMember("my_view_count", Value(video.my_view_count), allocator);
        cur_video.AddMember("last_watch_time", std::to_string(video.last_watch_time), allocator);
        video_array.PushBack(cur_video, allocator);
    }
    json_root.AddMember("videos", video_array, allocator);

    std::string data = RJson(json_root).dump();
    std::string file_path = PLAYLIST_FILE_PATH;

    std::ofstream file(file_path);
    if (file.is_open()) {
        file << data;
        file.close();
        logger.info("playlist/save", "playlist saved: " + playlist.name);
    } else {
        logger.warning("playlist/save", "failed to save playlist: " + playlist.name);
    }
}

void Playlist::add_video(const PlaylistVideo &video) {
    resource_lock.lock();
    videos.push_back(video);
    save_playlist();
    resource_lock.unlock();
}

void Playlist::remove_video(const std::string &video_id) {
    resource_lock.lock();
    videos.erase(std::remove_if(videos.begin(), videos.end(), [&video_id](const PlaylistVideo &video) {
        return video.id == video_id;
    }), videos.end());
    save_playlist();
    resource_lock.unlock();
}

void Playlist::add_to_queue(const PlaylistVideo &video) {
    resource_lock.lock();
    queue.push_back(video);
    resource_lock.unlock();
}

void Playlist::remove_from_queue(const std::string &video_id) {
    resource_lock.lock();
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&video_id](const PlaylistVideo &video) {
        return video.id == video_id;
    }), queue.end());
    resource_lock.unlock();
}

PlaylistVideo Playlist::get_next_video() {
    resource_lock.lock();
    if (!queue.empty()) {
        PlaylistVideo next_video = queue.front();
        queue.erase(queue.begin());
        resource_lock.unlock();
        return next_video;
    }
    resource_lock.unlock();
    return PlaylistVideo();
}

std::vector<PlaylistVideo> Playlist::get_all_videos() {
    resource_lock.lock();
    auto res = videos;
    resource_lock.unlock();
    return res;
}

std::vector<PlaylistVideo> Playlist::get_queue() {
    resource_lock.lock();
    auto res = queue;
    resource_lock.unlock();
    return res;
}

void Playlist::clear_queue() {
    resource_lock.lock();
    queue.clear();
    resource_lock.unlock();
}

void Playlist::set_continuous_playback(bool enable) {
    resource_lock.lock();
    continuous_playback = enable;
    save_playlist();
    resource_lock.unlock();
}

bool Playlist::is_continuous_playback_enabled() {
    resource_lock.lock();
    bool res = continuous_playback;
    resource_lock.unlock();
    return res;
}

void add_video_to_playlist(PlaylistVideo video) {
    playlist.add_video(video);
}

void remove_video_from_playlist(const std::string &video_id) {
    playlist.remove_video(video_id);
}

std::vector<PlaylistVideo> get_all_videos() {
    return playlist.get_all_videos();
}

std::vector<PlaylistVideo> get_queue() {
    return playlist.get_queue();
}

PlaylistVideo get_next_video() {
    return playlist.get_next_video();
}

void clear_queue() {
    playlist.clear_queue();
}

void set_continuous_playback(bool enable) {
    playlist.set_continuous_playback(enable);
}

bool is_continuous_playback_enabled() {
    return playlist.is_continuous_playback_enabled();
}