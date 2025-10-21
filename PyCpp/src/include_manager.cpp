#include "include_manager.h"
#include <algorithm>

IncludeManager::IncludeManager() {
    // Initialize with common headers that are almost always needed
    used_features.insert("iostream");
    used_features.insert("string");
    used_features.insert("vector");
}

void IncludeManager::trackFeature(const std::string& feature) {
    used_features.insert(feature);
}

void IncludeManager::trackManualInclude(const std::string& header) {
    manual_includes.insert(header);
}

std::vector<std::string> IncludeManager::getRequiredIncludes() const {
    std::unordered_set<std::string> headers;

    // Add headers for all used features
    for (const auto& feature : used_features) {
        auto it = feature_headers.find(feature);
        if (it != feature_headers.end()) {
            for (const auto& header : it->second) {
                headers.insert(header);
            }
        }
    }

    // Add manual includes
    for (const auto& header : manual_includes) {
        headers.insert(header);
    }

    // Convert to vector and sort for consistent output
    std::vector<std::string> result(headers.begin(), headers.end());
    std::sort(result.begin(), result.end());

    return result;
}

bool IncludeManager::isHeaderIncluded(const std::string& header) const {
    // Check if this specific header is in our manual includes
    if (manual_includes.find(header) != manual_includes.end()) {
        return true;
    }

    // Check if any feature requires this header
    for (const auto& feature : used_features) {
        auto it = feature_headers.find(feature);
        if (it != feature_headers.end()) {
            for (const auto& h : it->second) {
                if (h == header) {
                    return true;
                }
            }
        }
    }

    return false;
}