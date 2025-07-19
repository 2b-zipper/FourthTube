#include "headers.hpp"

double Util_convert_time_to_seconds(const std::string& timestamp_str) {
	if (timestamp_str.empty()) {
		return -1.0;
	}

	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	int parsed_count = 0;

	// try H:MM:SS or HH:MM:SS format
	parsed_count = sscanf(timestamp_str.c_str(), "%d:%02d:%02d", &hours, &minutes, &seconds);
	if (parsed_count == 3) {
		if (hours >= 0 && minutes >= 0 && minutes < 60 && seconds >= 0 && seconds < 60) {
			return hours * 3600.0 + minutes * 60.0 + seconds;
		}
	}

	// try MM:SS format
	parsed_count = sscanf(timestamp_str.c_str(), "%d:%02d", &minutes, &seconds);
	if (parsed_count == 2) {
		if (minutes >= 0 && seconds >= 0 && seconds < 60) {
			return minutes * 60.0 + seconds;
		}
	}

	return -1.0;
}

int Util_find_timestamp_in_text(const std::string& text, int start_pos, int* timestamp_start, int* timestamp_end, double* timestamp_seconds) {
	if (timestamp_start == NULL || timestamp_end == NULL || timestamp_seconds == NULL) {
		return -1;
	}

	int text_len = text.length();
	if (start_pos >= text_len || text_len < 4) {
		return -1;
	}

	for (int i = start_pos; i < text_len - 3; i++) {
		if (isdigit(text[i])) {
			int digit_start = i;
			int colon_count = 0;
			int j = i;
			
			while (j < text_len && (isdigit(text[j]) || text[j] == ':')) {
				if (text[j] == ':') {
					colon_count++;
					if (colon_count > 2) break;
				}
				j++;
			}

			if (colon_count == 1 || colon_count == 2) {
				int timestamp_len = j - digit_start;
				if (timestamp_len >= 4 && timestamp_len <= 8) {
					std::string timestamp_candidate = text.substr(digit_start, timestamp_len);

					double parsed_seconds = Util_convert_time_to_seconds(timestamp_candidate);
					if (parsed_seconds >= 0.0) {
						*timestamp_start = digit_start;
						*timestamp_end = j;
						*timestamp_seconds = parsed_seconds;
						return digit_start;
					}
				}
			}

			i = j - 1;
		}
	}

	return -1;
}
