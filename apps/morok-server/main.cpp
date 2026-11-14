#include "morok/common/build_info.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr std::size_t kMaxHeaderBytes = 16U * 1024U;
constexpr std::size_t kMaxBodyBytes = 64U * 1024U;
volatile std::sig_atomic_t running = 1;
void stop_server(int) { running = 0; }

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7fU) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ffU) {
        out.push_back(static_cast<char>(0xc0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else if (cp <= 0xffffU) {
        out.push_back(static_cast<char>(0xe0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else {
        out.push_back(static_cast<char>(0xf0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    }
}
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::uint32_t parse_hex4(std::string_view value, std::size_t& pos) {
    if (pos + 4 > value.size()) throw std::runtime_error("truncated JSON unicode escape");
    std::uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const int digit = hex_value(value[pos++]);
        if (digit < 0) throw std::runtime_error("invalid JSON unicode escape");
        result = (result << 4U) | static_cast<std::uint32_t>(digit);
    }
    return result;
}
std::string json_string(std::string_view value, std::size_t& pos) {
    if (pos >= value.size() || value[pos++] != '"')
        throw std::runtime_error("expected JSON string");
    std::string result;
    while (pos < value.size()) {
        const unsigned char c = static_cast<unsigned char>(value[pos++]);
        if (c == '"') return result;
        if (c < 0x20U) throw std::runtime_error("control character in JSON string");
        if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
        if (pos >= value.size()) throw std::runtime_error("truncated JSON escape");
        const char escaped = value[pos++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
            std::uint32_t cp = parse_hex4(value, pos);
            if (cp >= 0xd800U && cp <= 0xdbffU) {
                if (pos + 2 > value.size() || value[pos++] != '\\' || value[pos++] != 'u')
                    throw std::runtime_error("missing low surrogate");
                const auto low = parse_hex4(value, pos);
                if (low < 0xdc00U || low > 0xdfffU)
                    throw std::runtime_error("invalid low surrogate");
                cp = 0x10000U + ((cp - 0xd800U) << 10U) + (low - 0xdc00U);
            } else if (cp >= 0xdc00U && cp <= 0xdfffU) {
                throw std::runtime_error("unexpected low surrogate");
            }
            append_utf8(result, cp);
            break;
        }
        default: throw std::runtime_error("invalid JSON escape");
        }
    }
    throw std::runtime_error("unterminated JSON string");
}
std::string extract_prompt(std::string_view body) {
    const auto key = body.find("\"prompt\"");
    if (key == std::string_view::npos) throw std::runtime_error("missing prompt field");
    auto pos = body.find(':', key + 8);
    if (pos == std::string_view::npos) throw std::runtime_error("invalid prompt field");
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    auto result = json_string(body, pos);
    if (!morok::common::Utf8Text::from_utf8(result))
        throw std::runtime_error("prompt must be valid UTF-8");
    return result;
}
std::size_t extract_max_bytes(std::string_view body) {
    const auto key = body.find("\"max_new_bytes\"");
    if (key == std::string_view::npos) return 256;
    auto pos = body.find(':', key + 15);
    if (pos == std::string_view::npos) throw std::runtime_error("invalid max_new_bytes");
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    std::size_t value = 0;
    const auto start = pos;
    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
        const auto digit = static_cast<std::size_t>(body[pos++] - '0');
        if (value > 4096U / 10U) throw std::runtime_error("max_new_bytes out of range");
        value = value * 10U + digit;
    }
    if (pos == start || value == 0 || value > 4096)
        throw std::runtime_error("max_new_bytes must be between 1 and 4096");
    return value;
}
std::string escape_json(std::string_view value) {
    std::string result;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20U) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(hex[c >> 4U]);
                result.push_back(hex[c & 0x0fU]);
            } else result.push_back(static_cast<char>(c));
        }
    }
    return result;
}
bool send_all(int socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = ::send(socket, data.data(), data.size(), 0);
        if (sent <= 0) return false;
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}
void respond(int client, int status, std::string_view reason, std::string body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
           << "Content-Type: application/json; charset=utf-8\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "X-Content-Type-Options: nosniff\r\n\r\n";
    (void)send_all(client, header.str());
    (void)send_all(client, body);
}
void handle_client(int client, const morok::model::G1Parameters& parameters) {
    std::string request;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) return;
        request.append(buffer, static_cast<std::size_t>(count));
        if (request.size() > kMaxHeaderBytes) {
            respond(client, 431, "Request Header Fields Too Large", "{\"error\":\"headers too large\"}");
            return;
        }
        header_end = request.find("\r\n\r\n");
    }
    const auto first_end = request.find("\r\n");
    if (first_end == std::string::npos) {
        respond(client, 400, "Bad Request", "{\"error\":\"malformed request\"}");
        return;
    }
    const std::string first_line = request.substr(0, first_end);
    const auto first_space = first_line.find(' ');
    const auto second_space = first_line.find(' ', first_space == std::string::npos ? first_space : first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        respond(client, 400, "Bad Request", "{\"error\":\"malformed request line\"}");
        return;
    }
    const auto method = first_line.substr(0, first_space);
    const auto target = first_line.substr(first_space + 1, second_space - first_space - 1);
    if (method == "GET" && target == "/health") {
        respond(client, 200, "OK", "{\"status\":\"ok\",\"model\":\"G1\",\"version\":\"" +
                std::string(morok::common::version()) + "\"}");
        return;
    }
    if (method != "POST" || target != "/v1/generate") {
        respond(client, 404, "Not Found", "{\"error\":\"route not found\"}");
        return;
    }
    std::size_t content_length = 0;
    std::istringstream headers(request.substr(first_end + 2, header_end - first_end - 2));
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "transfer-encoding") {
            respond(client, 400, "Bad Request", "{\"error\":\"transfer encoding is unsupported\"}");
            return;
        }
        if (name == "content-length") {
            const auto value = line.substr(colon + 1);
            try { content_length = static_cast<std::size_t>(std::stoull(value)); }
            catch (...) { respond(client, 400, "Bad Request", "{\"error\":\"invalid content length\"}"); return; }
        }
    }
    if (content_length == 0 || content_length > kMaxBodyBytes) {
        respond(client, 413, "Payload Too Large", "{\"error\":\"body must be between 1 and 65536 bytes\"}");
        return;
    }
    request.resize(header_end + 4);
    while (request.size() - (header_end + 4) < content_length) {
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            respond(client, 400, "Bad Request", "{\"error\":\"incomplete request body\"}");
            return;
        }
        request.append(buffer, static_cast<std::size_t>(count));
        if (request.size() - (header_end + 4) > content_length) {
            respond(client, 400, "Bad Request", "{\"error\":\"unexpected trailing request bytes\"}");
            return;
        }
    }
    try {
        const auto body = std::string_view(request).substr(header_end + 4, content_length);
        const auto prompt_text = extract_prompt(body);
        const auto prompt = morok::common::Utf8Text::from_utf8(prompt_text);
        const auto max_bytes = extract_max_bytes(body);
        const auto result = morok::model::generate_g1(
            parameters, *prompt.value_if(), {max_bytes});
        respond(client, 200, "OK",
            "{\"continuation\":\"" + escape_json(result.continuation.string()) +
            "\",\"generated_bytes\":" + std::to_string(result.generated_bytes) +
            ",\"candidate_evaluations\":" + std::to_string(result.candidate_evaluations) + "}");
    } catch (const std::exception& error) {
        respond(client, 400, "Bad Request", "{\"error\":\"" + escape_json(error.what()) + "\"}");
    }
}
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: morok-server <checkpoint.morok> [port=8080]\n"
                  << "Binds only to 127.0.0.1; development server without TLS/authentication.\n";
        return 2;
    }
    try {
        unsigned long port = 8080;
        if (argc == 3) port = std::stoul(argv[2]);
        if (port == 0 || port > 65535) throw std::invalid_argument("port must be 1..65535");
        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const int server = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) throw std::runtime_error("socket creation failed");
        int reuse = 1;
        (void)::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(server);
            throw std::runtime_error("bind to 127.0.0.1 failed");
        }
        if (::listen(server, 16) < 0) {
            ::close(server);
            throw std::runtime_error("listen failed");
        }
        std::signal(SIGINT, stop_server);
        std::signal(SIGTERM, stop_server);
        std::cout << "Morok local server listening on http://127.0.0.1:"
                  << port << " (Ctrl+C to stop)\n";
        while (running) {
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const int client = ::accept(server, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (client < 0) {
                if (!running) break;
                continue;
            }
            handle_client(client, checkpoint.parameters);
            ::close(client);
        }
        ::close(server);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-server: " << error.what() << '\n';
        return 1;
    }
}
