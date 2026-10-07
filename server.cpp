#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

namespace fs = std::filesystem;

static int          g_httpPort  = 5666;
static int          g_httpsPort = 5667;
static std::string  g_aliasesFile = "aliases.json";
static std::string  g_certFile    = "cert.pem";
static std::string  g_keyFile     = "key.pem";
static std::string  g_certPass    = "alias123";
static std::string  g_pfxFile     = "cert.pfx";

struct AliasesCache {
    std::map<std::string, std::string> aliases;
    fs::file_time_type lastWrite;
    AliasesCache() : lastWrite{} {}
};

static AliasesCache g_cache;
static std::mutex   g_cacheMutex;
static volatile bool g_running = true;

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
    return s.substr(a, b - a);
}

static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

static std::string escapeHtml(const std::string& str) {
    std::string r;
    r.reserve(str.size() * 2);
    for (char c : str) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        case '\'': r += "&#39;"; break;
        default: r += c;
        }
    }
    return r;
}

static std::map<std::string, std::string> loadJsonFlat(const std::string& path) {
    std::map<std::string, std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t brace1 = content.find('{');
    size_t brace2 = content.rfind('}');
    if (brace1 == std::string::npos || brace2 == std::string::npos || brace2 <= brace1)
        return result;
    std::string inner = content.substr(brace1 + 1, brace2 - brace1 - 1);
    size_t pos = 0;
    while (pos < inner.size()) {
        size_t k1 = inner.find('"', pos);
        if (k1 == std::string::npos) break;
        size_t k2 = inner.find('"', k1 + 1);
        if (k2 == std::string::npos) break;
        std::string key = inner.substr(k1 + 1, k2 - k1 - 1);
        size_t c = inner.find(':', k2 + 1);
        if (c == std::string::npos) break;
        size_t v1 = inner.find('"', c + 1);
        if (v1 == std::string::npos) break;
        size_t v2 = inner.find('"', v1 + 1);
        if (v2 == std::string::npos) break;
        std::string val = inner.substr(v1 + 1, v2 - v1 - 1);
        result[key] = val;
        pos = v2 + 1;
    }
    return result;
}

static std::map<std::string, std::string> getAliases() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    bool needReload = g_cache.aliases.empty();
    if (!needReload) {
        std::error_code ec;
        auto ft = fs::last_write_time(g_aliasesFile, ec);
        if (!ec && ft != g_cache.lastWrite) needReload = true;
    }
    if (needReload) {
        g_cache.aliases = loadJsonFlat(g_aliasesFile);
        std::error_code ec;
        g_cache.lastWrite = fs::last_write_time(g_aliasesFile, ec);
    }
    return g_cache.aliases;
}

static void printAliases() {
    auto aliases = getAliases();
    if (aliases.empty()) return;
    std::cout << "Aliases:" << std::endl;
    for (auto& [k, v] : aliases)
        std::cout << "  " << k << "  ->  " << v << std::endl;
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
};

static HttpRequest parseRequest(const std::string& raw) {
    HttpRequest req;
    size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) end = raw.find("\n\n");
    std::string headerBlock = (end != std::string::npos) ? raw.substr(0, end) : raw;
    size_t lineEnd = headerBlock.find("\r\n");
    if (lineEnd == std::string::npos) lineEnd = headerBlock.find('\n');
    std::string reqLine = (lineEnd != std::string::npos) ? headerBlock.substr(0, lineEnd) : headerBlock;
    size_t s1 = reqLine.find(' ');
    size_t s2 = reqLine.find(' ', s1 + 1);
    if (s1 != std::string::npos && s2 != std::string::npos) {
        req.method = reqLine.substr(0, s1);
        req.path   = reqLine.substr(s1 + 1, s2 - s1 - 1);
    }
    size_t pos = lineEnd + 2;
    while (pos < headerBlock.size()) {
        size_t nl = headerBlock.find("\r\n", pos);
        if (nl == std::string::npos) nl = headerBlock.find('\n', pos);
        std::string line = (nl != std::string::npos) ? headerBlock.substr(pos, nl - pos) : headerBlock.substr(pos);
        size_t col = line.find(':');
        if (col != std::string::npos) {
            std::string k = trim(line.substr(0, col));
            std::string v = trim(line.substr(col + 1));
            req.headers[toLower(k)] = v;
        }
        if (nl == std::string::npos) break;
        pos = nl + 2;
    }
    return req;
}

static std::string makeResponse(int code, const std::string& status,
    const std::map<std::string, std::string>& extraHeaders,
    const std::string& body, const std::string& contentType)
{
    time_t now = time(nullptr);
    char dateBuf[128];
    strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    std::string r = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
    r += "Date: " + std::string(dateBuf) + "\r\n";
    r += "Server: AliasRedirect/2.0\r\n";
    r += "Connection: close\r\n";
    r += "Content-Type: " + contentType + "\r\n";
    if (!body.empty())
        r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    for (auto& [k, v] : extraHeaders)
        r += k + ": " + v + "\r\n";
    r += "\r\n";
    r += body;
    return r;
}

static void handleClient(SOCKET sock, SSL* ssl) {
    char buf[65536];
    std::string data;
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n;
        if (ssl)
            n = SSL_read(ssl, buf + total, (int)(sizeof(buf) - 1 - total));
        else
            n = recv(sock, buf + total, (int)(sizeof(buf) - 1 - total), 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        data.assign(buf, total);
        if (data.find("\r\n\r\n") != std::string::npos) break;
    }
    if (data.empty()) {
        if (ssl) SSL_shutdown(ssl);
        return;
    }

    HttpRequest req = parseRequest(data);
    std::string host = req.headers["host"];
    size_t colonPos = host.find(':');
    if (colonPos != std::string::npos) host = host.substr(0, colonPos);
    host = toLower(host);

    std::string response;
    auto aliases = getAliases();
    auto it = aliases.find(host);

    if (it != aliases.end()) {
        std::string target = it->second;
        if (target.find("://") == std::string::npos) target = "https://" + target;
        response = makeResponse(302, "Found", {{"Location", target}}, "", "text/plain");
    } else if (req.method == "OPTIONS") {
        response = makeResponse(204, "No Content",
            {{"Access-Control-Allow-Origin", "*"},
             {"Access-Control-Allow-Methods", "GET, HEAD, POST, PUT, DELETE, OPTIONS"},
             {"Access-Control-Allow-Headers", "*"}}, "", "text/plain");
    } else {
        std::string rows;
        for (auto& [k, v] : aliases)
            rows += "<li><code>" + escapeHtml(k) + "</code> &rarr; <a href=\"" + escapeHtml(v) + "\">" + escapeHtml(v) + "</a></li>";
        std::string body =
            "<!DOCTYPE html><html lang=\"zh-CN\">"
            "<head><meta charset=\"UTF-8\"><title>Redirect Service</title>"
            "<style>"
            "body{font-family:'Microsoft YaHei',sans-serif;max-width:600px;margin:80px auto;padding:20px;background:#1e1e2e;color:#cdd6f4}"
            "h1{color:#cba6f7}a{color:#89b4fa}code{background:#313244;padding:2px 6px;border-radius:4px}li{margin:8px 0}"
            "</style></head><body>"
            "<h1>Redirect Service Running</h1>"
            "<p>Alias <code>" + escapeHtml(host) + "</code> not configured.</p>"
            "<p>Configured aliases:</p><ul>" + rows + "</ul>"
            "<p style=\"color:#6c7086;font-size:12px\">Edit aliases.json to add more</p>"
            "</body></html>";
        response = makeResponse(404, "Not Found", {}, body, "text/html; charset=utf-8");
    }

    if (ssl) {
        SSL_write(ssl, response.c_str(), (int)response.size());
        SSL_shutdown(ssl);
    } else {
        send(sock, response.c_str(), (int)response.size(), 0);
    }
}

static void runHttpServer() {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cerr << "HTTP socket failed: " << WSAGetLastError() << std::endl;
        return;
    }
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_httpPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "HTTP bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSock);
        return;
    }
    listen(listenSock, SOMAXCONN);
    std::cout << "HTTP  -> http://localhost:" << g_httpPort << std::endl;

    while (g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenSock, &fds);
        timeval tv{1, 0};
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        std::thread([client]() {
            handleClient(client, nullptr);
            closesocket(client);
        }).detach();
    }
    closesocket(listenSock);
}

static void runHttpsServer(SSL_CTX* ctx) {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cerr << "HTTPS socket failed: " << WSAGetLastError() << std::endl;
        return;
    }
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_httpsPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "HTTPS bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSock);
        return;
    }
    listen(listenSock, SOMAXCONN);
    std::cout << "HTTPS -> https://localhost:" << g_httpsPort << std::endl;

    while (g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenSock, &fds);
        timeval tv{1, 0};
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        std::thread([ctx, client]() {
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, (int)client);
            if (SSL_accept(ssl) > 0)
                handleClient(client, ssl);
            SSL_free(ssl);
            closesocket(client);
        }).detach();
    }
    closesocket(listenSock);
}

static void setConsoleTitle(const std::string& title) {
    SetConsoleTitleA(title.c_str());
}

static BOOL WINAPI ctrlHandler(DWORD) {
    g_running = false;
    return TRUE;
}

int main(int argc, char* argv[]) {
    setConsoleTitle("URL-Alias-Redirect C++");

    const char* envHttp  = std::getenv("HTTP_PORT");
    const char* envHttps = std::getenv("HTTPS_PORT");
    if (envHttp)  g_httpPort  = std::atoi(envHttp);
    if (envHttps) g_httpsPort = std::atoi(envHttps);

    fs::path exeDir = fs::path(argv[0]).parent_path();
    if (!exeDir.empty()) fs::current_path(exeDir);

    g_aliasesFile = (fs::path(exeDir) / "aliases.json").string();
    g_certFile    = (fs::path(exeDir) / "cert.pem").string();
    g_keyFile     = (fs::path(exeDir) / "key.pem").string();
    g_pfxFile     = (fs::path(exeDir) / "cert.pfx").string();

    std::cout << "==================================" << std::endl;
    std::cout << "  URL Alias Redirect (C++)" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    if (!fs::exists(g_aliasesFile)) {
        std::ofstream ofs(g_aliasesFile);
        ofs << "{}" << std::endl;
        std::cout << "Created default aliases.json" << std::endl;
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    std::thread httpThread;
    std::thread httpsThread;

    SSL_CTX* sslCtx = nullptr;
    bool httpsOk = fs::exists(g_certFile) && fs::exists(g_keyFile);
    if (httpsOk) {
        sslCtx = SSL_CTX_new(TLS_server_method());
        if (SSL_CTX_use_certificate_chain_file(sslCtx, g_certFile.c_str()) <= 0 ||
            SSL_CTX_use_PrivateKey_file(sslCtx, g_keyFile.c_str(), SSL_FILETYPE_PEM) <= 0 ||
            !SSL_CTX_check_private_key(sslCtx)) {
            std::cerr << "SSL cert/key load failed" << std::endl;
            SSL_CTX_free(sslCtx);
            sslCtx = nullptr;
            httpsOk = false;
        }
    }

    httpThread = std::thread(runHttpServer);
    if (httpsOk)
        httpsThread = std::thread([sslCtx]() { runHttpsServer(sslCtx); });

    if (!httpsOk) {
        std::cout << "No cert.pem + key.pem found, HTTPS disabled." << std::endl;
        if (fs::exists(g_pfxFile)) {
            std::cout << "Found cert.pfx. Convert to PEM:" << std::endl;
            std::cout << "  openssl pkcs12 -in cert.pfx -out cert.pem -clcerts -nokeys -passin pass:" << g_certPass << std::endl;
            std::cout << "  openssl pkcs12 -in cert.pfx -out key.pem -nocerts -nodes -passin pass:" << g_certPass << std::endl;
        } else {
            std::cout << "Run: powershell -File generate-cert.ps1" << std::endl;
        }
    }

    std::cout << std::endl;
    printAliases();
    std::cout << std::endl;
    if (httpsOk) {
        std::cout << "First visit: accept the self-signed cert warning in browser." << std::endl;
        std::cout << "Then type alias directly (no / needed)." << std::endl;
    }
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    httpThread.join();
    if (httpsThread.joinable()) httpsThread.join();

    if (sslCtx) SSL_CTX_free(sslCtx);
    WSACleanup();
    return 0;
}