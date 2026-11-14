#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ffprotocol::detail {

// Private settings implementation: one bounded structural parse, shared by
// normal settings and legacy shortcut migration. No regex field searches can
// accidentally read another section or depend on object member order.
struct SettingsJsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    std::wstring text;
    std::vector<SettingsJsonValue> elements;
    std::map<std::wstring, SettingsJsonValue, std::less<>> members;

    const SettingsJsonValue* Member(std::wstring_view name) const {
        if (kind != Kind::Object) return nullptr;
        const auto found = members.find(name);
        return found == members.end() ? nullptr : &found->second;
    }
};

class SettingsJsonReader {
public:
    explicit SettingsJsonReader(std::wstring_view text) : text_(text) {}

    std::optional<SettingsJsonValue> Read() {
        SettingsJsonValue result;
        if (!Value(result, 0)) return std::nullopt;
        Skip();
        if (position_ != text_.size() || result.kind != SettingsJsonValue::Kind::Object) return std::nullopt;
        return result;
    }

private:
    static bool Digit(wchar_t ch) { return ch >= L'0' && ch <= L'9'; }

    void Skip() {
        while (position_ < text_.size()) {
            const wchar_t ch = text_[position_];
            if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') break;
            ++position_;
        }
    }

    bool Take(wchar_t ch) {
        Skip();
        if (position_ >= text_.size() || text_[position_] != ch) return false;
        ++position_;
        return true;
    }

    bool Literal(std::wstring_view literal) {
        if (text_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    bool String(std::wstring& out) {
        if (!Take(L'"')) return false;
        while (position_ < text_.size()) {
            wchar_t ch = text_[position_++];
            if (ch == L'"') return true;
            if (ch < 0x20) return false;
            if (ch != L'\\') {
                out.push_back(ch);
                continue;
            }
            if (position_ >= text_.size()) return false;
            ch = text_[position_++];
            switch (ch) {
                case L'"': case L'\\': case L'/': out.push_back(ch); break;
                case L'b': out.push_back(L'\b'); break;
                case L'f': out.push_back(L'\f'); break;
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'u': {
                    unsigned codeUnit = 0;
                    for (int digit = 0; digit < 4; ++digit) {
                        if (position_ >= text_.size()) return false;
                        const wchar_t hex = text_[position_++];
                        codeUnit <<= 4;
                        if (hex >= L'0' && hex <= L'9') codeUnit += hex - L'0';
                        else if (hex >= L'a' && hex <= L'f') codeUnit += hex - L'a' + 10;
                        else if (hex >= L'A' && hex <= L'F') codeUnit += hex - L'A' + 10;
                        else return false;
                    }
                    out.push_back(static_cast<wchar_t>(codeUnit));
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool Number(SettingsJsonValue& out) {
        const size_t start = position_;
        if (position_ < text_.size() && text_[position_] == L'-') ++position_;
        if (position_ >= text_.size() || !Digit(text_[position_])) return false;
        if (text_[position_] == L'0') ++position_;
        else while (position_ < text_.size() && Digit(text_[position_])) ++position_;
        if (position_ < text_.size() && text_[position_] == L'.') {
            ++position_;
            if (position_ >= text_.size() || !Digit(text_[position_])) return false;
            while (position_ < text_.size() && Digit(text_[position_])) ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == L'e' || text_[position_] == L'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == L'+' || text_[position_] == L'-')) ++position_;
            if (position_ >= text_.size() || !Digit(text_[position_])) return false;
            while (position_ < text_.size() && Digit(text_[position_])) ++position_;
        }
        out.kind = SettingsJsonValue::Kind::Number;
        out.text.assign(text_.substr(start, position_ - start));
        return true;
    }

    bool Value(SettingsJsonValue& out, size_t depth) {
        Skip();
        if (position_ >= text_.size()) return false;
        const wchar_t ch = text_[position_];
        if (ch == L'{' || ch == L'[') {
            if (depth >= 64) return false;
            ++position_;
            const bool object = ch == L'{';
            out.kind = object ? SettingsJsonValue::Kind::Object : SettingsJsonValue::Kind::Array;
            const wchar_t end = object ? L'}' : L']';
            if (Take(end)) return true;
            do {
                std::wstring key;
                if (object && (!String(key) || !Take(L':'))) return false;
                SettingsJsonValue child;
                if (!Value(child, depth + 1)) return false;
                if (object) {
                    // Ambiguous duplicate keys fail closed, including aliases
                    // spelled with Unicode escapes.
                    if (!out.members.emplace(std::move(key), std::move(child)).second) return false;
                } else {
                    out.elements.push_back(std::move(child));
                }
                if (Take(end)) return true;
            } while (Take(L','));
            return false;
        }
        if (ch == L'"') {
            out.kind = SettingsJsonValue::Kind::String;
            return String(out.text);
        }
        if (ch == L't' || ch == L'f') {
            out.kind = SettingsJsonValue::Kind::Boolean;
            out.boolean = ch == L't';
            return Literal(out.boolean ? L"true" : L"false");
        }
        if (ch == L'n') return Literal(L"null");
        return Number(out);
    }

    std::wstring_view text_;
    size_t position_ = 0;
};

} // namespace ffprotocol::detail
