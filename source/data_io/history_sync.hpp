#pragma once
#include <vector>
#include "history.hpp"

std::vector<HistoryVideo> get_oauth_watch_history();
std::vector<HistoryVideo> get_oauth_watch_history(const std::string &continuation_token, std::string *out_next_token);
