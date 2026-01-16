#include "headers.hpp"
#include "history_sync.hpp"
#include "history.hpp"
#include "oauth/oauth.hpp"
#include "youtube_parser/internal_common.hpp"
#include "util/util.hpp"
#include "ui/views/specialized/succinct_video.hpp"
#include <set>

std::vector<HistoryVideo> get_oauth_watch_history() { return get_oauth_watch_history("", nullptr); }

std::vector<HistoryVideo> get_oauth_watch_history(const std::string &continuation_token, std::string *out_next_token) {
	std::vector<HistoryVideo> result;

	if (!OAuth::is_authenticated()) {
		logger.info("oauth-history", "Not authenticated");
		return result;
	}

	// Wait for internet connection
	u8 wifi_state = *(u8 *)0x1FF81067;
	int wifi_tries = 0;
	while (wifi_state != 2) {
		wifi_tries++;
		if (wifi_tries > 20) {
			logger.error("oauth-history", "no internet connection");
			return result;
		}
		logger.info("oauth-history", "waiting for internet connection...");
		usleep(1000000);
		wifi_state = *(u8 *)0x1FF81067;
		if (wifi_state == 2) {
			usleep(500000);
		}
	}

	RJson data;
	bool is_continuation = !continuation_token.empty();

	if (is_continuation) {
		data = OAuth::fetch_browse_data_with_continuation("FEhistory", continuation_token);
	} else {
		data = OAuth::fetch_browse_data("FEhistory");
	}

	if (!data.is_valid()) {
		logger.error("oauth-history", "fetch failed");
		return result;
	}

	std::set<std::string> seen_ids;

	RJson contents;
	if (is_continuation) {
		if (data.has_key("continuationContents") && data["continuationContents"].has_key("sectionListContinuation")) {
			contents = data["continuationContents"]["sectionListContinuation"]["contents"];
		}
	} else {
		if (data.has_key("contents") && data["contents"].has_key("singleColumnBrowseResultsRenderer") &&
		    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items().size() > 0 &&
		    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0].has_key("tabRenderer") &&
		    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"].has_key(
		        "content") &&
		    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]["content"].has_key(
		        "sectionListRenderer")) {
			contents = data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]
			               ["content"]["sectionListRenderer"]["contents"];
		}
	}

	for (auto section : contents.array_items()) {
		if (!section.has_key("itemSectionRenderer")) {
			continue;
		}

		auto items = section["itemSectionRenderer"]["contents"];

		for (auto item : items.array_items()) {
			RJson renderer;
			if (item.has_key("videoRenderer")) {
				renderer = item["videoRenderer"];
			} else if (item.has_key("compactVideoRenderer")) {
				renderer = item["compactVideoRenderer"];
			} else {
				continue;
			}

			if (!renderer.has_key("videoId")) {
				continue;
			}

			HistoryVideo video;
			video.id = renderer["videoId"].string_value();

			if (seen_ids.count(video.id)) {
				continue;
			}
			seen_ids.insert(video.id);

			if (renderer.has_key("title")) {
				video.title = youtube_parser::get_text_from_object(renderer["title"]);
			}

			if (renderer.has_key("ownerText")) {
				video.author_name = youtube_parser::get_text_from_object(renderer["ownerText"]);
			} else if (renderer.has_key("shortBylineText")) {
				video.author_name = youtube_parser::get_text_from_object(renderer["shortBylineText"]);
			}

			std::string view_count;
			if (renderer.has_key("shortViewCountText")) {
				view_count = youtube_parser::get_text_from_object(renderer["shortViewCountText"]);
			} else if (renderer.has_key("viewCountText")) {
				view_count = youtube_parser::get_text_from_object(renderer["viewCountText"]);
			}

			if (renderer.has_key("lengthText")) {
				video.length_text = youtube_parser::get_text_from_object(renderer["lengthText"]);
			}

			video.last_watch_time = time(nullptr);

			video.valid = youtube_is_valid_video_id(video.id);
			if (!video.valid) {
				logger.caution("oauth-history", "invalid history item : " + video.title);
			}

			video.title_lines = truncate_str(video.title, 320 - VIDEO_LIST_THUMBNAIL_WIDTH - 6, 2, 0.5, 0.5);

			video.view_count_text = view_count;

			if (video.valid && !video.title.empty()) {
				result.push_back(video);
			}
		}
	}

	// Extract continuation token
	if (out_next_token) {
		*out_next_token = "";
		RJson continuations;

		if (is_continuation) {
			if (data.has_key("continuationContents") &&
			    data["continuationContents"].has_key("sectionListContinuation") &&
			    data["continuationContents"]["sectionListContinuation"].has_key("continuations")) {
				continuations = data["continuationContents"]["sectionListContinuation"]["continuations"];
			}
		} else {
			if (data.has_key("contents") && data["contents"].has_key("singleColumnBrowseResultsRenderer") &&
			    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items().size() > 0 &&
			    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0].has_key("tabRenderer") &&
			    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]["content"]
			        .has_key("sectionListRenderer") &&
			    data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]["content"]
			        ["sectionListRenderer"]
			            .has_key("continuations")) {
				continuations = data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]
				                    ["content"]["sectionListRenderer"]["continuations"];
			}
		}

		if (continuations.array_items().size() > 0 && continuations[(size_t)0].has_key("nextContinuationData") &&
		    continuations[(size_t)0]["nextContinuationData"].has_key("continuation")) {
			*out_next_token = continuations[(size_t)0]["nextContinuationData"]["continuation"].string_value();
		}
	}

	logger.info("oauth-history", "Loaded " + std::to_string(result.size()) + " OAuth history items");
	return result;
}
