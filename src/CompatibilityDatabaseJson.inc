constexpr std::uintmax_t kMaxDatabaseBytes = 4ull * 1024ull * 1024ull;
constexpr std::size_t kMaxEntries = 4096;
constexpr std::size_t kMaxStringBytes = 1024;
constexpr std::size_t kMaxIssues = 64;
constexpr unsigned kMaxJsonDepth = 16;

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue, std::less<>> object;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    std::optional<JsonValue> Parse() {
        auto value = ParseValue(0);
        SkipSpace();
        if (!value || pos_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    void SkipSpace() noexcept {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++pos_;
        }
    }

    bool Consume(char c) noexcept {
        SkipSpace();
        if (pos_ >= text_.size() || text_[pos_] != c) return false;
        ++pos_;
        return true;
    }

    std::optional<std::string> ParseString() {
        SkipSpace();
        if (pos_ >= text_.size() || text_[pos_] != '"') return std::nullopt;
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            const unsigned char raw = static_cast<unsigned char>(text_[pos_++]);
            if (raw == '"') return out;
            if (raw < 0x20u) return std::nullopt;
            if (raw != '\\') {
                if (out.size() >= kMaxStringBytes) return std::nullopt;
                out.push_back(static_cast<char>(raw));
                continue;
            }
            if (pos_ >= text_.size()) return std::nullopt;
            const char escaped = text_[pos_++];
            char decoded = 0;
            switch (escaped) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            default: return std::nullopt;
            }
            if (out.size() >= kMaxStringBytes) return std::nullopt;
            out.push_back(decoded);
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseValue(unsigned depth) {
        if (depth > kMaxJsonDepth) return std::nullopt;
        SkipSpace();
        if (pos_ >= text_.size()) return std::nullopt;
        if (text_[pos_] == '{') return ParseObject(depth + 1);
        if (text_[pos_] == '[') return ParseArray(depth + 1);
        if (text_[pos_] == '"') {
            auto value = ParseString();
            if (!value) return std::nullopt;
            JsonValue out; out.type = JsonValue::Type::String; out.string = std::move(*value); return out;
        }
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4; JsonValue out; out.type = JsonValue::Type::Bool; out.boolean = true; return out;
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5; JsonValue out; out.type = JsonValue::Type::Bool; return out;
        }
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4; return JsonValue{};
        }

        const std::size_t begin = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            pos_ = begin; return std::nullopt;
        }
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                pos_ = begin; return std::nullopt;
            }
        } else {
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            bool fraction = false;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                fraction = true; ++pos_;
            }
            if (!fraction) { pos_ = begin; return std::nullopt; }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            bool exponent = false;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                exponent = true; ++pos_;
            }
            if (!exponent) { pos_ = begin; return std::nullopt; }
        }
        double number = 0.0;
        const char* first = text_.data() + begin;
        const char* last = text_.data() + pos_;
        const auto parsed = std::from_chars(first, last, number, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != last) { pos_ = begin; return std::nullopt; }
        JsonValue out; out.type = JsonValue::Type::Number; out.number = number; return out;
    }

    std::optional<JsonValue> ParseArray(unsigned depth) {
        if (!Consume('[')) return std::nullopt;
        JsonValue out; out.type = JsonValue::Type::Array;
        SkipSpace();
        if (Consume(']')) return out;
        while (out.array.size() <= kMaxEntries) {
            auto value = ParseValue(depth);
            if (!value) return std::nullopt;
            out.array.push_back(std::move(*value));
            SkipSpace();
            if (Consume(']')) return out;
            if (!Consume(',')) return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> ParseObject(unsigned depth) {
        if (!Consume('{')) return std::nullopt;
        JsonValue out; out.type = JsonValue::Type::Object;
        SkipSpace();
        if (Consume('}')) return out;
        while (out.object.size() <= kMaxEntries) {
            auto key = ParseString();
            if (!key || !Consume(':')) return std::nullopt;
            auto value = ParseValue(depth);
            if (!value) return std::nullopt;
            if (!out.object.emplace(std::move(*key), std::move(*value)).second) return std::nullopt;
            SkipSpace();
            if (Consume('}')) return out;
            if (!Consume(',')) return std::nullopt;
        }
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};
