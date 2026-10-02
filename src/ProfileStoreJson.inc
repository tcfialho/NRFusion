constexpr std::uintmax_t kMaxProfileFileBytes = 4ull * 1024ull * 1024ull;
constexpr std::size_t kMaxProfiles = 4096;
constexpr std::size_t kMaxFingerprintFieldBytes = 512;

std::string Escape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::optional<std::vector<std::string>> TopLevelKeys(std::string_view object) {
    std::vector<std::string> keys;
    int objectDepth = 0;
    int arrayDepth = 0;
    bool rootStarted = false;
    bool rootClosed = false;
    for (std::size_t pos = 0; pos < object.size();) {
        const char c = object[pos];
        if (!rootStarted) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++pos; continue; }
            if (c != '{') return std::nullopt;
            rootStarted = true;
            objectDepth = 1;
            ++pos;
            continue;
        }
        if (rootClosed) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return std::nullopt;
            ++pos;
            continue;
        }
        if (c == '{') { ++objectDepth; ++pos; continue; }
        if (c == '}') {
            --objectDepth;
            if (objectDepth < 0) return std::nullopt;
            if (objectDepth == 0) {
                if (arrayDepth != 0) return std::nullopt;
                rootClosed = true;
            }
            ++pos;
            continue;
        }
        if (c == '[') { ++arrayDepth; ++pos; continue; }
        if (c == ']') { --arrayDepth; if (arrayDepth < 0) return std::nullopt; ++pos; continue; }
        if (c != '"') { ++pos; continue; }

        const std::size_t begin = ++pos;
        bool escaped = false;
        bool hadEscape = false;
        for (; pos < object.size(); ++pos) {
            const char ch = object[pos];
            if (escaped) { escaped = false; hadEscape = true; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') break;
            if (static_cast<unsigned char>(ch) < 0x20u) return std::nullopt;
        }
        if (pos >= object.size() || escaped) return std::nullopt;
        const std::size_t finish = pos++;
        std::size_t after = pos;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        if (objectDepth == 1 && arrayDepth == 0 && after < object.size() && object[after] == ':') {
            if (hadEscape) return std::nullopt;
            keys.emplace_back(object.substr(begin, finish - begin));
        }
    }
    if (!rootStarted || !rootClosed || objectDepth != 0 || arrayDepth != 0) return std::nullopt;
    return keys;
}

template <std::size_t N>
bool HasExactKeys(std::string_view object, const std::array<std::string_view, N>& expected) {
    const auto keys = TopLevelKeys(object);
    if (!keys || keys->size() != expected.size()) return false;
    for (const auto& key : *keys) {
        if (std::count(keys->begin(), keys->end(), key) != 1) return false;
        if (std::find(expected.begin(), expected.end(), key) == expected.end()) return false;
    }
    return true;
}

std::optional<std::size_t> ValueStart(std::string_view object, std::string_view key) {
    int objectDepth = 0;
    int arrayDepth = 0;
    for (std::size_t pos = 0; pos < object.size();) {
        const char c = object[pos];
        if (c == '{') { ++objectDepth; ++pos; continue; }
        if (c == '}') { --objectDepth; if (objectDepth < 0) return std::nullopt; ++pos; continue; }
        if (c == '[') { ++arrayDepth; ++pos; continue; }
        if (c == ']') { --arrayDepth; if (arrayDepth < 0) return std::nullopt; ++pos; continue; }
        if (c != '"') { ++pos; continue; }

        const std::size_t begin = ++pos;
        bool escaped = false;
        bool hadEscape = false;
        for (; pos < object.size(); ++pos) {
            const char ch = object[pos];
            if (escaped) { escaped = false; hadEscape = true; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') break;
            if (static_cast<unsigned char>(ch) < 0x20u) return std::nullopt;
        }
        if (pos >= object.size() || escaped) return std::nullopt;
        const std::size_t finish = pos++;
        std::size_t after = pos;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        if (objectDepth != 1 || arrayDepth != 0 || after >= object.size() || object[after] != ':') continue;
        if (hadEscape || object.substr(begin, finish - begin) != key) continue;
        ++after;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        return after;
    }
    return std::nullopt;
}

std::optional<std::string> StringValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start || *start >= object.size() || object[*start] != '"') return std::nullopt;
    std::size_t pos = *start + 1;
    std::string out;
    bool escaped = false;
    for (; pos < object.size(); ++pos) {
        const char c = object[pos];
        if (escaped) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '/': out += '/'; break;
            default: return std::nullopt;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            std::size_t end = pos + 1;
            while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                           object[end] == '\r' || object[end] == '\n')) ++end;
            if (end != object.size() && object[end] != ',' && object[end] != '}' && object[end] != ']')
                return std::nullopt;
            return out;
        } else {
            if (static_cast<unsigned char>(c) < 0x20u) return std::nullopt;
            out += c;
        }
    }
    return std::nullopt;
}

std::optional<double> NumberValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start) return std::nullopt;
    std::size_t end = *start;
    if (end < object.size() && object[end] == '-') ++end;
    if (end >= object.size() || !std::isdigit(static_cast<unsigned char>(object[end])))
        return std::nullopt;
    if (object[end] == '0') {
        ++end;
        if (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])))
            return std::nullopt;
    } else {
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
    }
    if (end < object.size() && object[end] == '.') {
        ++end;
        const std::size_t fractionBegin = end;
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
        if (end == fractionBegin) return std::nullopt;
    }
    if (end < object.size() && (object[end] == 'e' || object[end] == 'E')) {
        ++end;
        if (end < object.size() && (object[end] == '+' || object[end] == '-')) ++end;
        const std::size_t exponentBegin = end;
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
        if (end == exponentBegin) return std::nullopt;
    }

    double value = 0.0;
    const char* begin = object.data() + *start;
    const char* finish = object.data() + end;
    const auto [ptr, ec] = std::from_chars(begin, finish, value, std::chars_format::general);
    if (ec != std::errc{} || ptr != finish || !std::isfinite(value)) return std::nullopt;
    while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                   object[end] == '\r' || object[end] == '\n')) ++end;
    if (end < object.size() && object[end] != ',' && object[end] != '}' && object[end] != ']')
        return std::nullopt;
    return value;
}

std::optional<bool> BoolValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start) return std::nullopt;
    const std::size_t pos = *start;
    const auto validDelimiter = [&](std::size_t end) {
        while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                       object[end] == '\r' || object[end] == '\n')) ++end;
        return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']';
    };
    if (object.substr(pos, 4) == "true" && validDelimiter(pos + 4)) return true;
    if (object.substr(pos, 5) == "false" && validDelimiter(pos + 5)) return false;
    return std::nullopt;
}

std::optional<std::vector<std::string_view>> ProfileObjects(const std::string& json) {
    std::vector<std::string_view> objects;
    const std::string_view view(json);
    const auto profilesStart = ValueStart(view, "profiles");
    if (!profilesStart || *profilesStart >= view.size() || view[*profilesStart] != '[') return std::nullopt;
    std::size_t pos = *profilesStart + 1;

    const auto skipWhitespace = [&]() {
        while (pos < view.size() && (view[pos] == ' ' || view[pos] == '\t' ||
                                     view[pos] == '\r' || view[pos] == '\n')) ++pos;
    };

    const auto finishArray = [&]() -> std::optional<std::vector<std::string_view>> {
        if (pos >= view.size() || view[pos] != ']') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos >= view.size() || view[pos] != '}') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos != view.size()) return std::nullopt;
        return objects;
    };

    skipWhitespace();
    if (pos < view.size() && view[pos] == ']') return finishArray();

    while (pos < view.size()) {
        skipWhitespace();
        if (pos >= view.size() || view[pos] != '{') return std::nullopt;
        const std::size_t start = pos;
        bool inString = false;
        bool escaped = false;
        int depth = 0;
        bool closed = false;
        for (; pos < view.size(); ++pos) {
            const char c = view[pos];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                if (depth <= 0) return std::nullopt;
                --depth;
                if (depth == 0) {
                    objects.push_back(view.substr(start, pos - start + 1));
                    ++pos;
                    closed = true;
                    break;
                }
            }
        }
        if (!closed || inString || depth != 0) return std::nullopt;

        skipWhitespace();
        if (pos >= view.size()) return std::nullopt;
        if (view[pos] == ']') return finishArray();
        if (view[pos] != ',') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos >= view.size() || view[pos] == ']') return std::nullopt;
    }
    return std::nullopt;
}
