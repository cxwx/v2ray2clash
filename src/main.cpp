#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Proxy {
    std::string type;
    std::string name;
    std::map<std::string, std::string> fields;
};

struct ParsedShareUrl {
    std::string credentials;
    std::string server;
    std::string port;
    std::map<std::string, std::string> query;
    std::string fragment;
};

namespace {

std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split(const std::string& input, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return text;
    }

    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string urlDecode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const std::string hex = input.substr(i + 1, 2);
            char* end = nullptr;
            long value = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        if (input[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string base64Normalize(std::string input) {
    input.erase(std::remove_if(input.begin(), input.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), input.end());
    std::replace(input.begin(), input.end(), '-', '+');
    std::replace(input.begin(), input.end(), '_', '/');
    while (input.size() % 4 != 0) {
        input.push_back('=');
    }
    return input;
}

std::string base64Decode(const std::string& raw) {
    static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string input = base64Normalize(raw);
    std::string output;
    int val = 0;
    int valb = -8;

    for (unsigned char ch : input) {
        if (ch == '=') {
            break;
        }
        std::size_t index = table.find(ch);
        if (index == std::string::npos) {
            throw std::runtime_error("invalid base64 content");
        }
        val = (val << 6) + static_cast<int>(index);
        valb += 6;
        if (valb >= 0) {
            output.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return output;
}

std::map<std::string, std::string> parseJsonObject(const std::string& json) {
    std::map<std::string, std::string> result;
    std::size_t i = 0;

    auto skipSpace = [&]() {
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
    };

    auto parseString = [&]() -> std::string {
        if (i >= json.size() || json[i] != '"') {
            throw std::runtime_error("expected JSON string");
        }
        ++i;
        std::string value;
        while (i < json.size()) {
            char ch = json[i++];
            if (ch == '\\') {
                if (i >= json.size()) {
                    throw std::runtime_error("invalid JSON escape");
                }
                char esc = json[i++];
                switch (esc) {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    default: value.push_back(esc); break;
                }
            } else if (ch == '"') {
                return value;
            } else {
                value.push_back(ch);
            }
        }
        throw std::runtime_error("unterminated JSON string");
    };

    skipSpace();
    if (i >= json.size() || json[i] != '{') {
        throw std::runtime_error("expected JSON object");
    }
    ++i;

    while (true) {
        skipSpace();
        if (i < json.size() && json[i] == '}') {
            ++i;
            break;
        }

        std::string key = parseString();
        skipSpace();
        if (i >= json.size() || json[i] != ':') {
            throw std::runtime_error("expected ':'");
        }
        ++i;
        skipSpace();

        std::string value;
        if (i < json.size() && json[i] == '"') {
            value = parseString();
        } else {
            std::size_t start = i;
            while (i < json.size() && json[i] != ',' && json[i] != '}') {
                ++i;
            }
            value = trim(json.substr(start, i - start));
        }

        result[key] = value;
        skipSpace();
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == '}') {
            ++i;
            break;
        }
    }

    return result;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> params;
    for (const std::string& pair : split(query, '&')) {
        if (pair.empty()) {
            continue;
        }
        std::size_t pos = pair.find('=');
        if (pos == std::string::npos) {
            params[urlDecode(pair)] = "";
            continue;
        }
        params[urlDecode(pair.substr(0, pos))] = urlDecode(pair.substr(pos + 1));
    }
    return params;
}

std::map<std::string, std::string> parseSemicolonPairs(const std::string& text) {
    std::map<std::string, std::string> result;
    for (const std::string& rawPart : split(text, ';')) {
        std::string part = trim(rawPart);
        if (part.empty()) {
            continue;
        }
        std::size_t pos = part.find('=');
        if (pos == std::string::npos) {
            result[toLower(part)] = "true";
            continue;
        }
        result[toLower(trim(part.substr(0, pos)))] = trim(part.substr(pos + 1));
    }
    return result;
}

std::string yamlEscape(const std::string& value) {
    std::string escaped = replaceAll(value, "\\", "\\\\");
    escaped = replaceAll(escaped, "\"", "\\\"");
    return "\"" + escaped + "\"";
}

std::string shellEscapeSingleQuotes(const std::string& value) {
    std::string escaped = "'";
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

std::string normalizeWsPath(const std::string& path) {
    if (path.empty()) {
        return "/";
    }
    if (path.front() == '/') {
        return path;
    }
    return "/" + path;
}

std::string ensureUniqueName(const std::string& requested, std::set<std::string>& usedNames) {
    std::string base = trim(requested).empty() ? "node" : trim(requested);
    std::string candidate = base;
    int suffix = 2;
    while (usedNames.find(candidate) != usedNames.end()) {
        candidate = base + "-" + std::to_string(suffix++);
    }
    usedNames.insert(candidate);
    return candidate;
}

void parseServerPort(const std::string& hostPort, const std::string& defaultPort,
                     std::string& server, std::string& port) {
    port = defaultPort;
    if (!hostPort.empty() && hostPort.front() == '[') {
        std::size_t bracket = hostPort.find(']');
        if (bracket == std::string::npos) {
            throw std::runtime_error("invalid IPv6 host");
        }
        server = hostPort.substr(1, bracket - 1);
        if (bracket + 1 < hostPort.size() && hostPort[bracket + 1] == ':') {
            port = hostPort.substr(bracket + 2);
        }
        return;
    }

    std::size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        server = hostPort.substr(0, colon);
        port = hostPort.substr(colon + 1);
    } else {
        server = hostPort;
    }
}

ParsedShareUrl parseShareUrl(const std::string& line, const std::string& scheme, const std::string& defaultPort) {
    const std::string prefix = scheme + "://";
    std::string payload = line.substr(prefix.size());

    ParsedShareUrl result;
    std::size_t hashPos = payload.find('#');
    if (hashPos != std::string::npos) {
        result.fragment = urlDecode(payload.substr(hashPos + 1));
        payload = payload.substr(0, hashPos);
    }

    std::string query;
    std::size_t questionPos = payload.find('?');
    if (questionPos != std::string::npos) {
        query = payload.substr(questionPos + 1);
        payload = payload.substr(0, questionPos);
    }

    std::size_t atPos = payload.rfind('@');
    if (atPos == std::string::npos) {
        throw std::runtime_error("missing '@' in " + scheme + " URL");
    }

    result.credentials = payload.substr(0, atPos);
    parseServerPort(payload.substr(atPos + 1), defaultPort, result.server, result.port);
    result.query = parseQuery(query);
    return result;
}

std::string decodeMaybeBase64Subscription(const std::string& input) {
    std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return input;
    }
    if (trimmed.find("://") != std::string::npos) {
        return input;
    }

    try {
        std::string decoded = base64Decode(trimmed);
        if (decoded.find("vmess://") != std::string::npos ||
            decoded.find("vless://") != std::string::npos ||
            decoded.find("trojan://") != std::string::npos ||
            decoded.find("ss://") != std::string::npos ||
            decoded.find("hysteria2://") != std::string::npos ||
            decoded.find("tuic://") != std::string::npos) {
            return decoded;
        }
    } catch (...) {
    }
    return input;
}

Proxy parseVmess(const std::string& line, std::set<std::string>& usedNames) {
    std::string payload = line.substr(std::string("vmess://").size());
    auto json = parseJsonObject(base64Decode(payload));

    Proxy proxy;
    proxy.type = "vmess";
    proxy.name = ensureUniqueName(json["ps"], usedNames);
    proxy.fields["server"] = json["add"];
    proxy.fields["port"] = json["port"].empty() ? "443" : json["port"];
    proxy.fields["uuid"] = json["id"];
    proxy.fields["alterId"] = json["aid"].empty() ? "0" : json["aid"];
    proxy.fields["cipher"] = json["scy"].empty() ? "auto" : json["scy"];
    proxy.fields["udp"] = "true";

    std::string tls = toLower(json["tls"]);
    if (tls == "tls" || tls == "1" || tls == "true") {
        proxy.fields["tls"] = "true";
    }

    std::string network = json["net"].empty() ? "tcp" : toLower(json["net"]);
    if (network != "tcp") {
        proxy.fields["network"] = network;
    }

    if (network == "ws") {
        proxy.fields["ws-opts.path"] = normalizeWsPath(json["path"]);
        if (!json["host"].empty()) {
            proxy.fields["ws-opts.headers.Host"] = json["host"];
        }
    } else if (network == "grpc") {
        proxy.fields["grpc-opts.grpc-service-name"] = json["path"];
    } else if (network == "h2" || network == "http") {
        proxy.fields["network"] = "h2";
        if (!json["path"].empty()) {
            proxy.fields["h2-opts.path"] = normalizeWsPath(json["path"]);
        }
        if (!json["host"].empty()) {
            proxy.fields["h2-opts.host.0"] = json["host"];
        }
    }

    if (!json["host"].empty() && network != "ws" && network != "h2" && network != "http") {
        proxy.fields["servername"] = json["host"];
    }
    if (!json["sni"].empty()) {
        proxy.fields["servername"] = json["sni"];
    }
    if (!json["alpn"].empty()) {
        int alpnIndex = 0;
        for (const std::string& item : split(json["alpn"], ',')) {
            if (!trim(item).empty()) {
                proxy.fields["alpn." + std::to_string(alpnIndex++)] = trim(item);
            }
        }
    }
    if (!json["fp"].empty()) {
        proxy.fields["client-fingerprint"] = json["fp"];
    }

    return proxy;
}

Proxy parseVlessOrTrojan(const std::string& line, const std::string& scheme, std::set<std::string>& usedNames) {
    ParsedShareUrl parsed = parseShareUrl(line, scheme, "443");
    auto& queryParams = parsed.query;

    Proxy proxy;
    proxy.type = scheme;
    proxy.name = ensureUniqueName(parsed.fragment.empty() ? parsed.server : parsed.fragment, usedNames);
    proxy.fields["server"] = parsed.server;
    proxy.fields["port"] = parsed.port;
    proxy.fields["udp"] = "true";

    if (scheme == "vless") {
        proxy.fields["uuid"] = parsed.credentials;
        proxy.fields["cipher"] = "auto";
        if (!queryParams["flow"].empty()) {
            proxy.fields["flow"] = queryParams["flow"];
        }
    } else if (scheme == "trojan") {
        proxy.fields["password"] = parsed.credentials;
    }

    std::string network = toLower(queryParams["type"]);
    if (network.empty()) {
        network = "tcp";
    }
    if (network != "tcp") {
        proxy.fields["network"] = network;
    }

    std::string security = toLower(queryParams["security"]);
    if (security == "tls" || security == "reality") {
        proxy.fields["tls"] = "true";
    }
    if (security == "reality") {
        proxy.fields["reality-opts.public-key"] = queryParams["pbk"];
        if (!queryParams["sid"].empty()) {
            proxy.fields["reality-opts.short-id"] = queryParams["sid"];
        }
        if (!queryParams["spx"].empty()) {
            proxy.fields["reality-opts.spider-x"] = queryParams["spx"];
        } else if (!queryParams["spiderx"].empty()) {
            proxy.fields["reality-opts.spider-x"] = queryParams["spiderx"];
        }
    }

    if (!queryParams["sni"].empty()) {
        proxy.fields["servername"] = queryParams["sni"];
    }
    if (!queryParams["fp"].empty()) {
        proxy.fields["client-fingerprint"] = queryParams["fp"];
    }
    if (!queryParams["alpn"].empty()) {
        int alpnIndex = 0;
        for (const std::string& item : split(queryParams["alpn"], ',')) {
            if (!trim(item).empty()) {
                proxy.fields["alpn." + std::to_string(alpnIndex++)] = trim(item);
            }
        }
    }
    if (queryParams["allowInsecure"] == "1" || toLower(queryParams["allowInsecure"]) == "true") {
        proxy.fields["skip-cert-verify"] = "true";
    }

    if (network == "ws") {
        proxy.fields["ws-opts.path"] = normalizeWsPath(queryParams["path"]);
        if (!queryParams["host"].empty()) {
            proxy.fields["ws-opts.headers.Host"] = queryParams["host"];
        }
    } else if (network == "grpc") {
        if (!queryParams["serviceName"].empty()) {
            proxy.fields["grpc-opts.grpc-service-name"] = queryParams["serviceName"];
        } else if (!queryParams["service_name"].empty()) {
            proxy.fields["grpc-opts.grpc-service-name"] = queryParams["service_name"];
        }
        if (!queryParams["authority"].empty()) {
            proxy.fields["grpc-opts.authority"] = queryParams["authority"];
        }
        if (queryParams["mode"] == "multi" || queryParams["multiMode"] == "true" || queryParams["multiMode"] == "1") {
            proxy.fields["grpc-opts.multi-mode"] = "true";
        }
    } else if (network == "http" || network == "h2") {
        proxy.fields["network"] = "h2";
        if (!queryParams["path"].empty()) {
            proxy.fields["h2-opts.path"] = normalizeWsPath(queryParams["path"]);
        }
        if (!queryParams["host"].empty()) {
            proxy.fields["h2-opts.host.0"] = queryParams["host"];
        }
    }

    return proxy;
}

Proxy parseShadowsocks(const std::string& line, std::set<std::string>& usedNames) {
    std::string payload = line.substr(std::string("ss://").size());
    std::string fragment;
    std::size_t hashPos = payload.find('#');
    if (hashPos != std::string::npos) {
        fragment = urlDecode(payload.substr(hashPos + 1));
        payload = payload.substr(0, hashPos);
    }

    std::string pluginQuery;
    std::size_t questionPos = payload.find('?');
    if (questionPos != std::string::npos) {
        pluginQuery = payload.substr(questionPos + 1);
        payload = payload.substr(0, questionPos);
    }

    std::string userInfo;
    std::string hostPort;
    std::size_t atPos = payload.rfind('@');
    if (atPos != std::string::npos) {
        userInfo = payload.substr(0, atPos);
        hostPort = payload.substr(atPos + 1);
        if (userInfo.find(':') == std::string::npos) {
            userInfo = base64Decode(userInfo);
        }
    } else {
        std::string decoded = base64Decode(payload);
        std::size_t decodedAt = decoded.rfind('@');
        if (decodedAt == std::string::npos) {
            throw std::runtime_error("invalid ss URL");
        }
        userInfo = decoded.substr(0, decodedAt);
        hostPort = decoded.substr(decodedAt + 1);
    }

    std::size_t colonPos = userInfo.find(':');
    if (colonPos == std::string::npos) {
        throw std::runtime_error("invalid ss userinfo");
    }

    Proxy proxy;
    proxy.type = "ss";
    std::string server;
    std::string port;
    parseServerPort(hostPort, "8388", server, port);
    proxy.name = ensureUniqueName(fragment.empty() ? server : fragment, usedNames);
    proxy.fields["server"] = server;
    proxy.fields["port"] = port;
    proxy.fields["cipher"] = userInfo.substr(0, colonPos);
    proxy.fields["password"] = userInfo.substr(colonPos + 1);
    proxy.fields["udp"] = "true";

    auto queryParams = parseQuery(pluginQuery);
    if (!queryParams["plugin"].empty()) {
        proxy.fields["plugin"] = queryParams["plugin"];
        if (!queryParams["plugin-opts"].empty()) {
            auto pluginOpts = parseSemicolonPairs(queryParams["plugin-opts"]);
            for (const auto& [key, value] : pluginOpts) {
                if (key == "tls") {
                    proxy.fields["plugin-opts.tls"] = "true";
                } else {
                    proxy.fields["plugin-opts." + key] = value;
                }
            }
        }
    }

    return proxy;
}

Proxy parseHysteria2(const std::string& line, std::set<std::string>& usedNames) {
    ParsedShareUrl parsed = parseShareUrl(line, "hysteria2", "443");

    Proxy proxy;
    proxy.type = "hysteria2";
    proxy.name = ensureUniqueName(parsed.fragment.empty() ? parsed.server : parsed.fragment, usedNames);
    proxy.fields["server"] = parsed.server;
    proxy.fields["port"] = parsed.port;
    proxy.fields["password"] = parsed.credentials;
    proxy.fields["udp"] = "true";

    if (!parsed.query["sni"].empty()) {
        proxy.fields["sni"] = parsed.query["sni"];
    }
    if (!parsed.query["insecure"].empty() &&
        (parsed.query["insecure"] == "1" || toLower(parsed.query["insecure"]) == "true")) {
        proxy.fields["skip-cert-verify"] = "true";
    }
    if (!parsed.query["obfs"].empty()) {
        proxy.fields["obfs"] = parsed.query["obfs"];
    }
    if (!parsed.query["obfs-password"].empty()) {
        proxy.fields["obfs-password"] = parsed.query["obfs-password"];
    }
    if (!parsed.query["upmbps"].empty()) {
        proxy.fields["up"] = parsed.query["upmbps"];
    }
    if (!parsed.query["downmbps"].empty()) {
        proxy.fields["down"] = parsed.query["downmbps"];
    }

    return proxy;
}

Proxy parseTuic(const std::string& line, std::set<std::string>& usedNames) {
    ParsedShareUrl parsed = parseShareUrl(line, "tuic", "443");
    std::size_t colonPos = parsed.credentials.find(':');
    if (colonPos == std::string::npos) {
        throw std::runtime_error("invalid tuic credentials");
    }

    Proxy proxy;
    proxy.type = "tuic";
    proxy.name = ensureUniqueName(parsed.fragment.empty() ? parsed.server : parsed.fragment, usedNames);
    proxy.fields["server"] = parsed.server;
    proxy.fields["port"] = parsed.port;
    proxy.fields["uuid"] = parsed.credentials.substr(0, colonPos);
    proxy.fields["password"] = parsed.credentials.substr(colonPos + 1);
    proxy.fields["udp"] = "true";

    if (!parsed.query["sni"].empty()) {
        proxy.fields["sni"] = parsed.query["sni"];
    }
    if (!parsed.query["congestion_control"].empty()) {
        proxy.fields["congestion-controller"] = parsed.query["congestion_control"];
    }
    if (!parsed.query["udp_relay_mode"].empty()) {
        proxy.fields["udp-relay-mode"] = parsed.query["udp_relay_mode"];
    }
    if (!parsed.query["alpn"].empty()) {
        int alpnIndex = 0;
        for (const std::string& item : split(parsed.query["alpn"], ',')) {
            if (!trim(item).empty()) {
                proxy.fields["alpn." + std::to_string(alpnIndex++)] = trim(item);
            }
        }
    }
    if (!parsed.query["allow_insecure"].empty() &&
        (parsed.query["allow_insecure"] == "1" || toLower(parsed.query["allow_insecure"]) == "true")) {
        proxy.fields["skip-cert-verify"] = "true";
    }

    return proxy;
}

void emitField(std::ostream& out, const std::string& key, const std::string& value, int indent = 4) {
    out << std::string(indent, ' ') << key << ": ";
    if (value == "true" || value == "false") {
        out << value << '\n';
        return;
    }

    bool isNumber = !value.empty() &&
                    std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    if (isNumber) {
        out << value << '\n';
        return;
    }
    out << yamlEscape(value) << '\n';
}

void emitProxy(std::ostream& out, const Proxy& proxy) {
    out << "  - name: " << yamlEscape(proxy.name) << '\n';
    out << "    type: " << proxy.type << '\n';

    const auto emitSimpleField = [&](const std::string& name) {
        auto it = proxy.fields.find(name);
        if (it != proxy.fields.end() && !it->second.empty()) {
            emitField(out, name, it->second);
        }
    };

    for (const std::string& name : {"server", "port", "uuid", "password", "alterId", "cipher", "flow", "udp",
                                    "tls", "servername", "sni", "skip-cert-verify", "client-fingerprint",
                                    "network", "obfs", "obfs-password", "up", "down", "congestion-controller",
                                    "udp-relay-mode", "plugin"}) {
        emitSimpleField(name);
    }

    std::vector<std::string> alpns;
    for (int i = 0;; ++i) {
        auto it = proxy.fields.find("alpn." + std::to_string(i));
        if (it == proxy.fields.end()) {
            break;
        }
        alpns.push_back(it->second);
    }
    if (!alpns.empty()) {
        out << "    alpn:\n";
        for (const auto& item : alpns) {
            out << "      - " << yamlEscape(item) << '\n';
        }
    }

    auto wsPath = proxy.fields.find("ws-opts.path");
    auto wsHost = proxy.fields.find("ws-opts.headers.Host");
    if (wsPath != proxy.fields.end() || wsHost != proxy.fields.end()) {
        out << "    ws-opts:\n";
        if (wsPath != proxy.fields.end()) {
            emitField(out, "path", wsPath->second, 6);
        }
        if (wsHost != proxy.fields.end()) {
            out << "      headers:\n";
            emitField(out, "Host", wsHost->second, 8);
        }
    }

    auto grpcService = proxy.fields.find("grpc-opts.grpc-service-name");
    auto grpcAuthority = proxy.fields.find("grpc-opts.authority");
    auto grpcMultiMode = proxy.fields.find("grpc-opts.multi-mode");
    if ((grpcService != proxy.fields.end() && !grpcService->second.empty()) ||
        (grpcAuthority != proxy.fields.end() && !grpcAuthority->second.empty()) ||
        (grpcMultiMode != proxy.fields.end() && !grpcMultiMode->second.empty())) {
        out << "    grpc-opts:\n";
        if (grpcService != proxy.fields.end() && !grpcService->second.empty()) {
            emitField(out, "grpc-service-name", grpcService->second, 6);
        }
        if (grpcAuthority != proxy.fields.end() && !grpcAuthority->second.empty()) {
            emitField(out, "authority", grpcAuthority->second, 6);
        }
        if (grpcMultiMode != proxy.fields.end() && !grpcMultiMode->second.empty()) {
            emitField(out, "multi-mode", grpcMultiMode->second, 6);
        }
    }

    auto h2Path = proxy.fields.find("h2-opts.path");
    auto h2Host = proxy.fields.find("h2-opts.host.0");
    if (h2Path != proxy.fields.end() || h2Host != proxy.fields.end()) {
        out << "    h2-opts:\n";
        if (h2Host != proxy.fields.end() && !h2Host->second.empty()) {
            out << "      host:\n";
            out << "        - " << yamlEscape(h2Host->second) << '\n';
        }
        if (h2Path != proxy.fields.end() && !h2Path->second.empty()) {
            emitField(out, "path", h2Path->second, 6);
        }
    }

    auto realityKey = proxy.fields.find("reality-opts.public-key");
    auto realitySid = proxy.fields.find("reality-opts.short-id");
    auto realitySpiderX = proxy.fields.find("reality-opts.spider-x");
    if (realityKey != proxy.fields.end() && !realityKey->second.empty()) {
        out << "    reality-opts:\n";
        emitField(out, "public-key", realityKey->second, 6);
        if (realitySid != proxy.fields.end() && !realitySid->second.empty()) {
            emitField(out, "short-id", realitySid->second, 6);
        }
        if (realitySpiderX != proxy.fields.end() && !realitySpiderX->second.empty()) {
            emitField(out, "spider-x", realitySpiderX->second, 6);
        }
    }

    std::vector<std::string> pluginOptKeys;
    for (const auto& [key, value] : proxy.fields) {
        if (startsWith(key, "plugin-opts.") && !value.empty()) {
            pluginOptKeys.push_back(key.substr(std::string("plugin-opts.").size()));
        }
    }
    std::sort(pluginOptKeys.begin(), pluginOptKeys.end());
    if (!pluginOptKeys.empty()) {
        out << "    plugin-opts:\n";
        for (const std::string& key : pluginOptKeys) {
            emitField(out, key, proxy.fields.at("plugin-opts." + key), 6);
        }
    }
}

std::vector<Proxy> parseInput(const std::string& rawInput) {
    std::vector<Proxy> proxies;
    std::set<std::string> usedNames;
    std::string normalized = decodeMaybeBase64Subscription(rawInput);
    std::stringstream ss(normalized);
    std::string line;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        try {
            if (startsWith(line, "vmess://")) {
                proxies.push_back(parseVmess(line, usedNames));
            } else if (startsWith(line, "ss://")) {
                proxies.push_back(parseShadowsocks(line, usedNames));
            } else if (startsWith(line, "vless://")) {
                proxies.push_back(parseVlessOrTrojan(line, "vless", usedNames));
            } else if (startsWith(line, "trojan://")) {
                proxies.push_back(parseVlessOrTrojan(line, "trojan", usedNames));
            } else if (startsWith(line, "hysteria2://")) {
                proxies.push_back(parseHysteria2(line, usedNames));
            } else if (startsWith(line, "tuic://")) {
                proxies.push_back(parseTuic(line, usedNames));
            }
        } catch (const std::exception& ex) {
            std::cerr << "skip node: " << ex.what() << '\n';
        }
    }
    return proxies;
}

void writeConfig(std::ostream& out, const std::vector<Proxy>& proxies) {
    out << "mixed-port: 7890\n";
    out << "allow-lan: false\n";
    out << "mode: rule\n";
    out << "log-level: info\n";
    out << "ipv6: true\n";
    out << "dns:\n";
    out << "  enable: true\n";
    out << "  listen: 0.0.0.0:1053\n";
    out << "  enhanced-mode: fake-ip\n";
    out << "  nameserver:\n";
    out << "    - 223.5.5.5\n";
    out << "    - 1.1.1.1\n";
    out << "\n";
    out << "proxies:\n";
    for (const auto& proxy : proxies) {
        emitProxy(out, proxy);
    }

    out << "\n";
    out << "proxy-groups:\n";
    out << "  - name: " << yamlEscape("PROXY") << '\n';
    out << "    type: select\n";
    out << "    proxies:\n";
    out << "      - " << yamlEscape("AUTO") << '\n';
    out << "      - " << yamlEscape("DIRECT") << '\n';
    for (const auto& proxy : proxies) {
        out << "      - " << yamlEscape(proxy.name) << '\n';
    }
    out << "  - name: " << yamlEscape("AUTO") << '\n';
    out << "    type: url-test\n";
    out << "    url: " << yamlEscape("http://www.gstatic.com/generate_204") << '\n';
    out << "    interval: 300\n";
    out << "    proxies:\n";
    for (const auto& proxy : proxies) {
        out << "      - " << yamlEscape(proxy.name) << '\n';
    }

    out << "\n";
    out << "rules:\n";
    out << "  - MATCH,PROXY\n";
}

std::string readAll(std::istream& input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string fetchUrl(const std::string& url, const std::vector<std::string>& headers) {
    std::ostringstream cmd;
    cmd << "curl -fsSL --connect-timeout 10 --max-time 60 ";
    for (const auto& header : headers) {
        cmd << "-H " << shellEscapeSingleQuotes(header) << ' ';
    }
    cmd << shellEscapeSingleQuotes(url);

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.str().c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("failed to start curl");
    }

    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe.get()) != nullptr) {
        output += buffer;
    }

    int status = pclose(pipe.release());
    if (status != 0) {
        throw std::runtime_error("curl failed for URL: " + url);
    }
    return output;
}

void printHelp(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " -i input.txt [-o config.yaml]\n"
        << "  " << program << " -u https://example.com/subscription [-o config.yaml]\n"
        << "  " << program << " < input.txt > config.yaml\n\n"
        << "Supported links: vmess, vless, trojan, ss, hysteria2, tuic\n"
        << "Output is aimed at Clash.Meta / Mihomo.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string inputFile;
    std::string outputFile;
    std::string subscriptionUrl;
    std::vector<std::string> headers;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            inputFile = argv[++i];
        } else if ((arg == "-u" || arg == "--url") && i + 1 < argc) {
            subscriptionUrl = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputFile = argv[++i];
        } else if ((arg == "-H" || arg == "--header") && i + 1 < argc) {
            headers.push_back(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            printHelp(argv[0]);
            return 1;
        }
    }

    std::string inputData;
    if (!inputFile.empty() && !subscriptionUrl.empty()) {
        std::cerr << "choose either --input or --url\n";
        return 1;
    }

    if (!subscriptionUrl.empty()) {
        try {
            inputData = fetchUrl(subscriptionUrl, headers);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << '\n';
            return 1;
        }
    } else if (!inputFile.empty()) {
        std::ifstream in(inputFile);
        if (!in) {
            std::cerr << "failed to open input: " << inputFile << '\n';
            return 1;
        }
        inputData = readAll(in);
    } else {
        inputData = readAll(std::cin);
    }

    std::vector<Proxy> proxies = parseInput(inputData);
    if (proxies.empty()) {
        std::cerr << "no supported nodes found\n";
        return 1;
    }

    if (!outputFile.empty()) {
        std::ofstream out(outputFile);
        if (!out) {
            std::cerr << "failed to open output: " << outputFile << '\n';
            return 1;
        }
        writeConfig(out, proxies);
    } else {
        writeConfig(std::cout, proxies);
    }

    std::cerr << "converted " << proxies.size() << " nodes\n";
    return 0;
}
