#include "config/StyleTree.hpp"

#include "config/StyleValues.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    // Past the deepest block any shape holds, so the passes that recurse once per level stay shallow.
    constexpr size_t kMaxLevels = 8;

    // What a selector starts with, and what can follow a name in one. None is a name character.
    constexpr std::string_view kSelectorCharacters = ".#*:[>,~+";
    constexpr std::string_view kSelectorSyntax     = ".*[>,~+";

    // Parses style.conf's blocks (key { ... }) and properties (key: value), counting lines for its errors. The format
    // looks enough like CSS that users write CSS in it, so each CSS habit it does not take is refused by name.
    class TreeParser {
      public:
        TreeParser(std::string_view text, std::string_view source) : _text(text), _source(source) {}

        std::vector<StyleNode> Parse() {
            return ParseBlockBody(0, nullptr);
        }

      private:
        std::string_view _text;
        std::string_view _source;
        size_t           _at   = 0;
        std::uint32_t    _line = 1;

        bool             AtEnd() const {
            return _at >= _text.size();
        }

        std::string_view Rest() const {
            return _text.substr(_at);
        }

        ConfigError Error(std::uint32_t line, std::string_view message) const {
            return StyleError(_source, line, message);
        }

        // Spaces, line breaks and // comments, none of which the format reads.
        void SkipBlank() {
            while (!AtEnd()) {
                const char character = _text[_at];
                if (character == '\n') {
                    ++_line;
                    ++_at;
                } else if (character == ' ' || character == '\t' || character == '\r') {
                    ++_at;
                } else if (Rest().starts_with("//")) {
                    _at = std::min(_text.size(), _text.find('\n', _at));
                } else if (Rest().starts_with("/*")) {
                    throw Error(_line, "/* */ is not a comment here: comments are // to the end of the line");
                } else {
                    return;
                }
            }
        }

        // The blocks and properties up to the } that closes opener, or to the end of the text for the top level.
        std::vector<StyleNode> ParseBlockBody(size_t level, const StyleNode* opener) {
            std::vector<StyleNode> children;
            // A map, not a find over children: a block of thousands of one-line entries would make that quadratic.
            std::map<std::string, std::uint32_t, std::less<>> keyLines;
            while (true) {
                SkipBlank();
                if (AtEnd()) {
                    if (opener != nullptr) {
                        throw Error(opener->line, std::format("{}: the block opened here is never closed: end it with }}", opener->key));
                    }
                    return children;
                }
                if (_text[_at] == '}') {
                    if (opener == nullptr) {
                        throw Error(_line, "} closes no block");
                    }
                    ++_at;
                    return children;
                }
                StyleNode node              = ParseEntry(level);
                const auto [earlier, added] = keyLines.try_emplace(node.key, node.line);
                if (!added) {
                    throw Error(node.line, std::format("{}: is already set on line {} of the same block", node.key, earlier->second));
                }
                children.push_back(std::move(node));
            }
        }

        // One block or property, from its key.
        StyleNode ParseEntry(size_t level) {
            const std::uint32_t line = _line;
            RefuseCssAtKey();
            const size_t start = _at;
            while (!AtEnd() && IsNameCharacter(_text[_at])) {
                ++_at;
            }
            const std::string key(_text.substr(start, _at - start));
            if (key.empty()) {
                throw Error(line, std::format("'{}' cannot start a key: write letters, digits, '_' and '-'", CharacterAt(_text, _at)));
            }
            SkipBlank();
            if (AtEnd()) {
                throw Error(line, std::format("{}: ends the file: follow it with : and a value, or with {{ to open a block", key));
            }
            const char next = _text[_at];
            if (next == '{') {
                if (level >= kMaxLevels) {
                    throw Error(line, std::format("{}: is nested {} levels deep, and a style nests at most {}", key, level + 1, kMaxLevels));
                }
                ++_at;
                StyleNode block{
                    .key      = key,
                    .value    = {},
                    .line     = line,
                    .children = {},
                };
                block.children = ParseBlockBody(level + 1, &block);
                if (block.children.empty()) {
                    throw Error(line, std::format("{}: is an empty block: a block holds at least one block or property", key));
                }
                return block;
            }
            if (next == ':') {
                ++_at;
                return StyleNode{
                    .key      = key,
                    .value    = ParseValue(key, line),
                    .line     = line,
                    .children = {},
                };
            }
            if (kSelectorSyntax.find(next) != std::string_view::npos) {
                throw SelectorError(line, _text.substr(start));
            }
            throw Error(line, std::format("{}: is followed by '{}': follow a key with : and a value, or with {{ to open a block", key, CharacterAt(_text, _at)));
        }

        // What a CSS rule or custom property starts with, in place of a key.
        void RefuseCssAtKey() const {
            const std::string_view rest = Rest();
            if (rest.starts_with(":root")) {
                throw Error(_line, ":root is CSS: define values in a vars block and write them as $name");
            }
            if (rest.starts_with("--")) {
                throw Error(_line, "--name is a CSS custom property: define it in a vars block and write it as $name");
            }
            if (rest.starts_with('@')) {
                const std::string_view rule = rest.substr(0, rest.find_first_of(" \t\r\n{;"));
                throw Error(_line, std::format("{} is a CSS at-rule, and style.conf has none", rule));
            }
            if (kSelectorCharacters.find(rest.front()) != std::string_view::npos) {
                throw SelectorError(_line, rest);
            }
        }

        ConfigError SelectorError(std::uint32_t line, std::string_view from) const {
            const std::string_view selector = Trim(from.substr(0, from.find_first_of("{\n")));
            return Error(line, std::format("'{}' is a selector, and style.conf has none: blocks are names nested inside each other, as item {{ hover {{ ... }} }}", selector));
        }

        // The value after a property's colon, which ends at a line break, a ;, its block's } or a comment.
        std::string ParseValue(std::string_view key, std::uint32_t line) {
            const size_t           end   = std::min({_text.find_first_of("\n;}", _at), _text.find("//", _at), _text.size()});
            const std::string_view value = Trim(_text.substr(_at, end - _at));
            _at                          = end;
            if (value.empty()) {
                throw Error(line, std::format("{}: has no value: a property is {}: value, and a block opens with {} {{", key, key, key));
            }
            if (value.find('{') != std::string_view::npos) {
                throw SelectorError(line, std::format("{}:{}", key, value));
            }
            if (value.find("!important") != std::string_view::npos) {
                throw Error(line, std::format("{}: !important is CSS: a property comes from the most specific block that sets it, never from a flag", key));
            }
            if (value.find("var(") != std::string_view::npos) {
                throw Error(line, std::format("{}: var() is CSS: define the value in a vars block and write it as $name", key));
            }
            if (!AtEnd() && _text[_at] == ';') {
                ++_at;
            }
            return std::string(value);
        }
    };

    using Variables = std::map<std::string, std::string, std::less<>>;

    Variables ResolveVariables(const std::vector<StyleNode>& roots, std::string_view source) {
        Variables  variables;
        const auto vars = std::ranges::find(roots, std::string_view("vars"), &StyleNode::key);
        if (vars == roots.end()) {
            return variables;
        }
        if (!IsBlock(*vars)) {
            throw StyleError(source, vars->line, "vars: is a block of name: value lines");
        }
        for (const StyleNode& variable : vars->children) {
            if (IsBlock(variable)) {
                throw StyleError(source, variable.line, std::format("vars > {}: vars holds name: value lines, not blocks", variable.key));
            }
            if (variable.value.find('$') != std::string::npos) {
                throw StyleError(source, variable.line, std::format("vars > {}: a var cannot use another var: give it a value", variable.key));
            }
            variables.emplace(variable.key, variable.value);
        }
        return variables;
    }

    // Many times any real value, and it bounds what vars can multiply a file into.
    constexpr size_t kMaxSubstitutedLength = 1024;

    // Every $name in value replaced with its vars value. A block's empty value comes back as it is.
    std::string Substitute(const StyleNode& property, const Variables& variables, std::string_view source) {
        std::string value;
        size_t      start = 0;
        for (size_t dollar = property.value.find('$'); dollar != std::string::npos; dollar = property.value.find('$', start)) {
            size_t end = dollar + 1;
            while (end < property.value.size() && IsNameCharacter(property.value[end])) {
                ++end;
            }
            const std::string_view name = std::string_view(property.value).substr(dollar + 1, end - dollar - 1);
            if (name.empty()) {
                throw StyleError(source, property.line, std::format("{}: $ must be followed by the name of a var", property.key));
            }
            const auto found = variables.find(name);
            if (found == variables.end()) {
                throw StyleError(source, property.line, std::format("{}: ${} names no var: define it in vars, as vars {{ {}: #e0def4 }}", property.key, name, name));
            }
            value += std::string_view(property.value).substr(start, dollar - start);
            value += found->second;
            if (value.size() > kMaxSubstitutedLength) {
                throw StyleError(source, property.line, std::format("{}: is longer than {} characters once its vars are filled in", property.key, kMaxSubstitutedLength));
            }
            start = end;
        }
        value += std::string_view(property.value).substr(start);
        return value;
    }

    std::vector<StyleNode> SubstituteAll(const std::vector<StyleNode>& nodes, const Variables& variables, std::string_view source) {
        std::vector<StyleNode> substituted;
        substituted.reserve(nodes.size());
        for (const StyleNode& node : nodes) {
            substituted.push_back(StyleNode{
                .key      = node.key,
                .value    = Substitute(node, variables, source),
                .line     = node.line,
                .children = SubstituteAll(node.children, variables, source),
            });
        }
        return substituted;
    }

} // namespace

ConfigError StyleError(std::string_view source, std::uint32_t line, std::string_view message) {
    return ConfigError(std::format("{}:{}: {}", source, line, message));
}

bool IsBlock(const StyleNode& node) {
    return node.value.empty();
}

const StyleNode* Child(const StyleNode* block, std::string_view key) {
    if (block == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(block->children, key, &StyleNode::key);
    if (found == block->children.end()) {
        return nullptr;
    }
    return &*found;
}

const StyleNode* TopLevel(const StyleSheet& sheet, std::string_view key) {
    const auto found = std::ranges::find(sheet.blocks, key, &StyleNode::key);
    if (found == sheet.blocks.end()) {
        return nullptr;
    }
    return &*found;
}

std::vector<StyleNode> ParseStyleTree(std::string_view text, std::string_view source) {
    const std::vector<StyleNode> tree      = TreeParser(text, source).Parse();
    const Variables              variables = ResolveVariables(tree, source);
    std::vector<StyleNode>       nodes     = SubstituteAll(tree, variables, source);
    std::erase_if(nodes, [](const StyleNode& node) { return node.key == "vars"; });
    return nodes;
}

bool IsNameCharacter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
}
