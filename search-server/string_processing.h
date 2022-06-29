#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <set>
#include <functional>

std::vector<std::string_view> SplitIntoWords(std::string_view str);

template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    std::set<std::string, std::less<>> non_empty_strings;
    for (const auto& str : strings) {
        if (!str.empty()) {
            non_empty_strings.insert(std::string{str});
        }
    }
    return non_empty_strings;
}
