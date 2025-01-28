#include <string>
#include <map>
#include <vector>
#include "parser.hpp"
#include "rapidjson_wrapper.hpp"
#define INNERTUBE_KEY "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8"

#	include "types.hpp"
#	include "system/file.hpp"
#	include "system/cpu_limit.hpp"
#	include "util/util.hpp"
#	include "util/log.hpp"
#	include "util/misc_tasks.hpp"
#	include "data_io/history.hpp"
#	include "network_decoder/network_io.hpp"
#	include "definitions.hpp"
#	define debug_info(s) logger.info("yt-parser", (s))
#	define debug_caution(s) logger.caution("yt-parser", (s))
#	define debug_warning(s) logger.warning("yt-parser", (s))
#	define debug_error(s) logger.error("yt-parser", (s))

using namespace rapidjson;

namespace youtube_parser {
	RJson get_error_json(const std::string &error);
	
	// internal state
	extern std::string language_code;
	extern std::string country_code;
	
	inline std::string get_innertube_api_url(std::string api_name) { return "https://m.youtube.com/youtubei/v1/" + api_name + "?key=" + INNERTUBE_KEY + "&prettyPrint=false"; }
	
	// network operation related
#	ifndef _WIN32
	extern NetworkSessionList thread_network_session_list;
	HttpRequest http_get_request(const std::string &url, std::map<std::string, std::string> headers = {});
	HttpRequest http_post_json_request(const std::string &url, const std::string &json, std::map<std::string, std::string> headers = {});
#	endif
	std::pair<bool, std::string> http_get(const std::string &url, std::map<std::string, std::string> header = {});
	std::pair<bool, std::string> http_post_json(const std::string &url, const std::string &json, std::map<std::string, std::string> header = {});
	
	// string util
	bool starts_with(const std::string &str, const std::string &pattern, size_t offset = 0);
	bool ends_with(const std::string &str, const std::string &pattern);
	
	// URL-related
	std::string url_decode(std::string input);
	
	// parse something like 'abc=def&ghi=jkl&lmn=opq'
	std::map<std::string, std::string> parse_parameters(std::string input);

	
	// youtube-specific
	std::string get_text_from_object(RJson json);
	YouTubeVideoSuccinct parse_succinct_video(RJson video_renderer);
	std::string get_thumbnail_url_closest(RJson thumbnails, int target_width);
	std::string get_thumbnail_url_exact(RJson thumbnails, int target_width); // modify the url to make its width match `target_width`
	

	// str[0] must be '(', '[', '{', or '\''
	// returns the prefix of str until the corresponding parenthesis or quote of str[0]
	std::string remove_garbage(const std::string &str, size_t start);
	// html can contain unnecessary garbage at the end of the actual json data
	RJson to_json(Document &json_root, const std::string &html, size_t start);

	// search for `var_name` = ' or `var_name` = {
	bool fast_extract_initial(Document &json_root, const std::string &html, const std::string &var_name, RJson &res);
	
	RJson get_succeeding_json_regexes(Document &json_root, const std::string &html, std::vector<const char *> patterns);
	
	
	// parses `str` as json and calls `on_success` or `on_fail` based on the result of the parsing
	// the content of `str` will be modified
	template<class Func1, class Func2>
	void parse_json_destructive(char *str, const Func1 &on_success, const Func2 &on_fail) {
		std::string json_err;
		Document json_root;
		RJson data = RJson::parse_inplace(json_root, str, json_err);
		if (json_err != "") on_fail(json_err);
		else on_success(json_root, data); // both `json_root` and `str` is alive at this point
	}
	// calls `access` to fetch json and calls `parse` to parse the json
	// properly handles the lifetime of json objects
	template<class Func1, class Func2, class Func3>
	void access_and_parse_json(const Func1 &access, const Func2 &on_success, const Func3 &on_fail) {
		auto result = access();
		if (result.first) parse_json_destructive(&result.second[0], on_success, on_fail);
		else on_fail(result.second);
	}
	
	std::string convert_url_to_mobile(std::string url);
	std::string convert_url_to_desktop(std::string url);
}
using namespace youtube_parser;

