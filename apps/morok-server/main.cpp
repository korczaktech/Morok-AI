#include "morok/common/build_info.hpp"
#include "morok/common/utf8_text.hpp"
#include "morok/model/learn/g1_checkpoint.hpp"
#include "morok/model/learn/g1_generation.hpp"
#include "morok/core/runtime.hpp"
#include "morok/core/session_registry.hpp"
#include "morok/memory/memory_subsystem.hpp"
#include "morok/lang/language_subsystem.hpp"
#include "morok/reason/reasoning_subsystem.hpp"
#include "morok/tools/tool_subsystem.hpp"
#include "morok/security/security_subsystem.hpp"
#include "morok/comm/session_manager.hpp"
#include "morok/learn/learning_subsystem.hpp"

#include <algorithm>
#include <charconv>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <utility>
#include <limits>
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
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
struct GenerateRequest final {
    std::string prompt;
    std::size_t max_new_bytes{256};
};
void skip_space(std::string_view value, std::size_t& pos) {
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) ++pos;
}
GenerateRequest parse_generate_request(std::string_view body) {
    std::size_t pos = 0;
    skip_space(body, pos);
    if (pos >= body.size() || body[pos++] != '{')
        throw std::runtime_error("request body must be a JSON object");
    GenerateRequest request;
    bool has_prompt = false;
    bool has_max_bytes = false;
    skip_space(body, pos);
    if (pos < body.size() && body[pos] == '}')
        throw std::runtime_error("prompt is required");
    while (pos < body.size()) {
        skip_space(body, pos);
        const auto key = json_string(body, pos);
        skip_space(body, pos);
        if (pos >= body.size() || body[pos++] != ':')
            throw std::runtime_error("expected colon after JSON property");
        skip_space(body, pos);
        if (key == "prompt") {
            if (has_prompt) throw std::runtime_error("duplicate prompt property");
            request.prompt = json_string(body, pos);
            has_prompt = true;
        } else if (key == "max_new_bytes") {
            if (has_max_bytes) throw std::runtime_error("duplicate max_new_bytes property");
            const auto first = pos;
            std::size_t number = 0;
            while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
                const auto digit = static_cast<std::size_t>(body[pos++] - '0');
                if (number > 256U / 10U)
                    throw std::runtime_error("max_new_bytes must be between 1 and 256");
                number = number * 10U + digit;
            }
            if (first == pos || number == 0 || number > 256)
                throw std::runtime_error("max_new_bytes must be an integer between 1 and 256");
            request.max_new_bytes = number;
            has_max_bytes = true;
        } else {
            throw std::runtime_error("unsupported JSON property: " + key);
        }
        skip_space(body, pos);
        if (pos >= body.size()) throw std::runtime_error("unterminated JSON object");
        if (body[pos] == '}') { ++pos; break; }
        if (body[pos++] != ',') throw std::runtime_error("expected comma in JSON object");
    }
    skip_space(body, pos);
    if (pos != body.size()) throw std::runtime_error("trailing data after JSON object");
    if (!has_prompt) throw std::runtime_error("prompt is required");
    if (!morok::common::Utf8Text::from_utf8(request.prompt))
        throw std::runtime_error("prompt must be valid UTF-8");
    return request;
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
        const auto sent = ::send(socket, data.data(), data.size(), MSG_NOSIGNAL);
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
void handle_client(int client, morok::comm::CommunicationSubsystem& communication) {
    std::string request;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) return;
        request.append(buffer, static_cast<std::size_t>(count));
        header_end = request.find("\r\n\r\n");
        if ((header_end == std::string::npos && request.size() > kMaxHeaderBytes) ||
            (header_end != std::string::npos && header_end > kMaxHeaderBytes)) {
            respond(client, 431, "Request Header Fields Too Large", "{\"error\":\"headers too large\"}");
            return;
        }
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
    if (method != "POST" || (target != "/v1/chat" && target != "/v1/generate")) {
        respond(client, 404, "Not Found", "{\"error\":\"route not found\"}");
        return;
    }
    std::size_t content_length = 0;
    bool has_content_length = false;
    bool has_json_content_type = false;
    std::istringstream headers(request.substr(first_end + 2, header_end - first_end - 2));
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            respond(client, 400, "Bad Request", "{\"error\":\"malformed HTTP header\"}");
            return;
        }
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string value = line.substr(colon + 1);
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t");
        value = first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
        if (name == "transfer-encoding") {
            respond(client, 400, "Bad Request", "{\"error\":\"transfer encoding is unsupported\"}");
            return;
        }
        if (name == "content-length") {
            if (has_content_length) {
                respond(client, 400, "Bad Request", "{\"error\":\"duplicate content length\"}");
                return;
            }
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), content_length);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                respond(client, 400, "Bad Request", "{\"error\":\"invalid content length\"}");
                return;
            }
            has_content_length = true;
        } else if (name == "content-type") {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            has_json_content_type = value == "application/json" ||
                value.rfind("application/json;", 0) == 0;
        }
    }
    if (!has_json_content_type) {
        respond(client, 415, "Unsupported Media Type", "{\"error\":\"Content-Type must be application/json\"}");
        return;
    }
    if (!has_content_length) {
        respond(client, 411, "Length Required", "{\"error\":\"Content-Length is required\"}");
        return;
    }
    if (content_length == 0 || content_length > kMaxBodyBytes) {
        respond(client, 413, "Payload Too Large", "{\"error\":\"body must be between 1 and 65536 bytes\"}");
        return;
    }
    const auto initial_body_bytes = request.size() - (header_end + 4);
    if (initial_body_bytes > content_length) {
        respond(client, 400, "Bad Request", "{\"error\":\"unexpected trailing request bytes\"}");
        return;
    }
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
        const auto parsed = parse_generate_request(body);
        const auto prompt = morok::common::Utf8Text::from_utf8(parsed.prompt);
        const auto max_bytes = parsed.max_new_bytes;
        auto conversation = communication.create_conversation();
        if (!conversation) throw std::runtime_error(conversation.error_if()->message);
        const auto conversation_id = *conversation.value_if();
        auto request_context = communication.create_request_context(conversation_id);
        if (!request_context) {
            (void)communication.close_conversation(conversation_id);
            throw std::runtime_error(request_context.error_if()->message);
        }
        const auto result = communication.send_message(
            *request_context.value_if(), conversation_id, *prompt.value_if(), max_bytes);
        (void)communication.close_conversation(conversation_id);
        if (!result) throw std::runtime_error(result.error_if()->message);
        respond(client, 200, "OK",
            "{\"response\":\"" + escape_json(result.value_if()->content) +
            "\",\"request_id\":\"" + result.value_if()->request_id.to_string() +
            "\"}");
    } catch (const std::exception& error) {
        respond(client, 400, "Bad Request", "{\"error\":\"" + escape_json(error.what()) + "\"}");
    }
}
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: morok-server <checkpoint.morok> [port=8080] [memory.sqlite]\n"
                  << "Binds only to 127.0.0.1; development server without TLS/authentication.\n";
        return 2;
    }
    try {
        unsigned long port = 8080;
        if (argc == 3) port = std::stoul(argv[2]);
        if (port == 0 || port > 65535) throw std::invalid_argument("port must be 1..65535");
        const auto checkpoint = morok::model::load_g1_checkpoint(argv[1]);
        const std::string memory_path = argc == 4 ? argv[3] : "morok-server-memory.sqlite";
        morok::core::RuntimeConfig runtime_config;
        runtime_config.runtime_name = "morok-server";
        runtime_config.max_sessions = 16;
        morok::core::SessionRegistry sessions(runtime_config.max_sessions);
        morok::memory::MemorySubsystem memory(memory_path);
        morok::lang::LanguageSubsystem language(checkpoint.parameters);
        morok::reason::ReasoningSubsystem reasoning;
        morok::tools::ToolAuditSink tool_audit = [](const morok::tools::ToolAuditRecord& record) {
            std::clog << "tool_audit event=" << static_cast<int>(record.type)
                      << " tool=" << record.tool_name << '\n';
            return morok::common::Status::success();
        };
        morok::security::SecurityAuditSink security_audit = [](const morok::security::SecurityAuditRecord& record) {
            std::clog << "security_audit event=" << static_cast<int>(record.type)
                      << " subject=" << record.subject_id
                      << " tool=" << record.tool_name << '\n';
            return morok::common::Status::success();
        };
        morok::tools::ToolSubsystem tools({}, std::move(tool_audit));
        morok::security::SecuritySubsystem security({}, std::move(security_audit));
        morok::comm::CommunicationSubsystem communication(sessions, language);
        morok::learn::LearningSubsystem learning;
        morok::core::Runtime runtime(runtime_config);
        const auto initialized = runtime.initialize(
            {memory.initializer(), language.initializer(), reasoning.initializer(),
             tools.initializer(), security.initializer(), communication.initializer(),
             learning.initializer()},
            {memory.shutdown_callback(), language.shutdown_callback(),
             reasoning.shutdown_callback(), tools.shutdown_callback(),
             security.shutdown_callback(), communication.shutdown_callback(),
             learning.shutdown_callback()});
        if (!initialized) throw std::runtime_error(initialized.error_if()->message);
        const int server = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) { (void)runtime.shutdown(); throw std::runtime_error("socket creation failed"); }
        int reuse = 1;
        (void)::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(server);
            (void)runtime.shutdown();
            throw std::runtime_error("bind to 127.0.0.1 failed");
        }
        if (::listen(server, 16) < 0) {
            ::close(server);
            (void)runtime.shutdown();
            throw std::runtime_error("listen failed");
        }
        struct sigaction signal_action {};
        signal_action.sa_handler = stop_server;
        sigemptyset(&signal_action.sa_mask);
        signal_action.sa_flags = 0;
        if (::sigaction(SIGINT, &signal_action, nullptr) != 0 ||
            ::sigaction(SIGTERM, &signal_action, nullptr) != 0) {
            ::close(server);
            (void)runtime.shutdown();
            throw std::runtime_error("could not install shutdown signal handlers");
        }
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
            const timeval client_timeout{5, 0};
            (void)::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                               &client_timeout, sizeof(client_timeout));
            (void)::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                               &client_timeout, sizeof(client_timeout));
            handle_client(client, communication);
            ::close(client);
        }
        ::close(server);
        (void)runtime.shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-server: " << error.what() << '\n';
        return 1;
    }
}
