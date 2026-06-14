// Placeholder for nlohmann/json single-header.
// Drop the real nlohmann/json.hpp (MIT, https://github.com/nlohmann/json) into this file
// before building. The vcxproj adds $(ProjectDir)include to the include path so
// `#include <nlohmann/json.hpp>` resolves to this file.
//
// To install: download release v3.11.3 from
//   https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
// and overwrite this file. No other build step required.
#pragma once
// Real nlohmann/json.hpp content goes here.
#include <map>
#include <string>
#include <vector>
namespace nlohmann {
    struct json {
        static json parse(const std::string&) { return {}; }
        bool is_null() const { return true; }
        template<typename T> T get() const { return T{}; }
        template<typename T> T value(const std::string&, const T& def) const { return def; }
        const json& operator[](const std::string&) const { return *this; }
    };
}
