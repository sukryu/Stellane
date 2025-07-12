#include “stellane/http/response.h”
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace stellane {

// ============================================================================
// Response 구현
// ============================================================================

Response::Response()
: status_code_(200)
, body_(””) {

```
// 기본 헤더 설정
with_header("Server", "Stellane/0.1.0");
update_content_length();
```

}

Response::Response(int status_code, std::string body)
: status_code_(status_code)
, body_(std::move(body)) {

```
// 기본 헤더 설정
with_header("Server", "Stellane/0.1.0");
update_content_length();
```

}

// ========================================================================
// 정적 팩토리 메서드 (2xx 성공)
// ========================================================================

Response Response::ok(std::string body) {
return Response(200, std::move(body));
}

Response Response::created(std::string body) {
return Response(201, std::move(body));
}

Response Response::accepted(std::string body) {
return Response(202, std::move(body));
}

Response Response::no_content() {
return Response(204, “”);
}

// ========================================================================
// 정적 팩토리 메서드 (3xx 리다이렉트)
// ========================================================================

Response Response::moved_permanently(const std::string& location) {
return Response(301).with_header(“Location”, location);
}

Response Response::found(const std::string& location) {
return Response(302).with_header(“Location”, location);
}

Response Response::not_modified() {
return Response(304, “”);
}

Response Response::redirect(const std::string& location) {
return found(location);
}

// ========================================================================
// 정적 팩토리 메서드 (4xx 클라이언트 오류)
// ========================================================================

Response Response::bad_request(std::string message) {
return Response(400, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::unauthorized(std::string message) {
return Response(401, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::forbidden(std::string message) {
return Response(403, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::not_found(std::string message) {
return Response(404, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::method_not_allowed(const std::vector<std::string>& allowed_methods) {
auto response = Response(405, “Method Not Allowed”)
.with_content_type(“text/plain; charset=utf-8”);

```
if (!allowed_methods.empty()) {
    std::ostringstream oss;
    for (size_t i = 0; i < allowed_methods.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << allowed_methods[i];
    }
    response.with_header("Allow", oss.str());
}

return response;
```

}

Response Response::conflict(std::string message) {
return Response(409, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::unprocessable_entity(std::string message) {
return Response(422, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::too_many_requests(std::optional<int> retry_after) {
auto response = Response(429, “Too Many Requests”)
.with_content_type(“text/plain; charset=utf-8”);

```
if (retry_after) {
    response.with_header("Retry-After", std::to_string(*retry_after));
}

return response;
```

}

// ========================================================================
// 정적 팩토리 메서드 (5xx 서버 오류)
// ========================================================================

Response Response::internal_error(std::string message) {
return Response(500, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::not_implemented(std::string message) {
return Response(501, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::bad_gateway(std::string message) {
return Response(502, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

Response Response::service_unavailable(std::string message) {
return Response(503, std::move(message))
.with_content_type(“text/plain; charset=utf-8”);
}

// ========================================================================
// 상태 및 본문 설정 (Fluent Interface)
// ========================================================================

Response& Response::with_status(int code) {
status_code_ = code;
return *this;
}

Response& Response::with_body(std::string body) {
body_ = std::move(body);
update_content_length();
return *this;
}

Response& Response::with_file(const std::string& file_path, const std::string& content_type) {
try {
std::ifstream file(file_path, std::ios::binary);
if (!file.is_open()) {
return with_status(404).with_body(“File not found”);
}

```
    // 파일 전체 읽기
    std::ostringstream oss;
    oss << file.rdbuf();
    body_ = oss.str();
    
    // Content-Type 설정
    if (!content_type.empty()) {
        with_content_type(content_type);
    } else {
        with_content_type(guess_content_type(file_path));
    }
    
    update_content_length();
    
} catch (const std::exception&) {
    return with_status(500).with_body("Failed to read file");
}

return *this;
```

}

// ========================================================================
// 헤더 설정
// ========================================================================

Response& Response::with_header(const std::string& key, const std::string& value) {
headers_[normalize_header_name(key)] = value;
return *this;
}

Response& Response::with_headers(const std::unordered_map<std::string, std::string>& headers) {
for (const auto& [key, value] : headers) {
with_header(key, value);
}
return *this;
}

Response& Response::with_content_type(const std::string& content_type) {
return with_header(“Content-Type”, content_type);
}

Response& Response::with_cache_control(const std::string& cache_control) {
return with_header(“Cache-Control”, cache_control);
}

Response& Response::with_cors(const std::string& origin,
const std::vector<std::string>& methods,
const std::vector<std::string>& headers) {

```
with_header("Access-Control-Allow-Origin", origin);

if (!methods.empty()) {
    std::ostringstream methods_oss;
    for (size_t i = 0; i < methods.size(); ++i) {
        if (i > 0) methods_oss << ", ";
        methods_oss << methods[i];
    }
    with_header("Access-Control-Allow-Methods", methods_oss.str());
}

if (!headers.empty()) {
    std::ostringstream headers_oss;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) headers_oss << ", ";
        headers_oss << headers[i];
    }
    with_header("Access-Control-Allow-Headers", headers_oss.str());
}

return *this;
```

}

// ========================================================================
// 쿠키 설정
// ========================================================================

Response& Response::with_cookie(const std::string& name,
const std::string& value,
const CookieOptions& options) {

```
std::string cookie_string = format_set_cookie(name, value, options);
set_cookies_.push_back(cookie_string);
return *this;
```

}

Response& Response::without_cookie(const std::string& name) {
CookieOptions options;
options.max_age = std::chrono::seconds(0);  // 즉시 만료
options.path = “/”;

```
return with_cookie(name, "", options);
```

}

// ========================================================================
// 접근자 메서드
// ========================================================================

int Response::status_code() const noexcept {
return status_code_;
}

const std::string& Response::body() const noexcept {
return body_;
}

size_t Response::content_length() const noexcept {
return body_.size();
}

const std::unordered_map<std::string, std::string>& Response::headers() const noexcept {
return headers_;
}

std::optional<std::string> Response::header(const std::string& key) const {
auto normalized_key = normalize_header_name(key);
auto it = headers_.find(normalized_key);
if (it != headers_.end()) {
return it->second;
}
return std::nullopt;
}

bool Response::has_header(const std::string& key) const {
return headers_.find(normalize_header_name(key)) != headers_.end();
}

std::optional<std::string> Response::content_type() const {
return header(“Content-Type”);
}

const std::vector<std::string>& Response::cookies() const noexcept {
return set_cookies_;
}

// ========================================================================
// 상태 확인 메서드
// ========================================================================

bool Response::is_success() const noexcept {
return status_code_ >= 200 && status_code_ < 300;
}

bool Response::is_redirect() const noexcept {
return status_code_ >= 300 && status_code_ < 400;
}

bool Response::is_client_error() const noexcept {
return status_code_ >= 400 && status_code_ < 500;
}

bool Response::is_server_error() const noexcept {
return status_code_ >= 500 && status_code_ < 600;
}

bool Response::is_error() const noexcept {
return is_client_error() || is_server_error();
}

bool Response::has_body() const noexcept {
return !body_.empty();
}

// ========================================================================
// 직렬화 및 디버그
// ========================================================================

std::string Response::to_http_string() const {
std::ostringstream oss;

```
// 상태 라인
oss << "HTTP/1.1 " << status_code_ << " " << status_message(status_code_) << "\r\n";

// 헤더들
for (const auto& [key, value] : headers_) {
    oss << key << ": " << value << "\r\n";
}

// Set-Cookie 헤더들
for (const auto& cookie : set_cookies_) {
    oss << "Set-Cookie: " << cookie << "\r\n";
}

// 빈 줄 (헤더와 본문 구분)
oss << "\r\n";

// 본문
oss << body_;

return oss.str();
```

}

std::string Response::summary() const {
std::ostringstream oss;
oss << status_code_ << “ “ << status_message(status_code_);
if (has_body()) {
oss << “ (” << content_length() << “ bytes)”;
}
return oss.str();
}

std::string Response::status_message(int status_code) {
static const std::unordered_map<int, std::string> status_messages = {
// 2xx Success
{200, “OK”},
{201, “Created”},
{202, “Accepted”},
{204, “No Content”},

```
    // 3xx Redirection
    {301, "Moved Permanently"},
    {302, "Found"},
    {304, "Not Modified"},
    
    // 4xx Client Error
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {409, "Conflict"},
    {422, "Unprocessable Entity"},
    {429, "Too Many Requests"},
    
    // 5xx Server Error
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"}
};

auto it = status_messages.find(status_code);
if (it != status_messages.end()) {
    return it->second;
}

// 범위별 기본 메시지
if (status_code >= 200 && status_code < 300) return "Success";
if (status_code >= 300 && status_code < 400) return "Redirection";
if (status_code >= 400 && status_code < 500) return "Client Error";
if (status_code >= 500 && status_code < 600) return "Server Error";

return "Unknown";
```

}

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

std::string Response::normalize_header_name(const std::string& header_name) {
// HTTP 헤더는 대소문자를 구분하지 않지만, 표준 형식으로 정규화
// 예: “content-type” → “Content-Type”
std::string normalized;
normalized.reserve(header_name.length());

```
bool capitalize_next = true;
for (char c : header_name) {
    if (c == '-') {
        normalized += '-';
        capitalize_next = true;
    } else if (capitalize_next) {
        normalized += std::toupper(c);
        capitalize_next = false;
    } else {
        normalized += std::tolower(c);
    }
}

return normalized;
```

}

std::string Response::format_set_cookie(const std::string& name,
const std::string& value,
const CookieOptions& options) {
std::ostringstream oss;

```
// 기본 name=value
oss << name << "=" << value;

// Max-Age 옵션
if (options.max_age) {
    oss << "; Max-Age=" << options.max_age->count();
}

// Domain 옵션
if (options.domain) {
    oss << "; Domain=" << *options.domain;
}

// Path 옵션
if (options.path) {
    oss << "; Path=" << *options.path;
}

// Secure 플래그
if (options.secure) {
    oss << "; Secure";
}

// HttpOnly 플래그
if (options.http_only) {
    oss << "; HttpOnly";
}

// SameSite 옵션
if (options.same_site) {
    oss << "; SameSite=" << *options.same_site;
}

return oss.str();
```

}

std::string Response::guess_content_type(const std::string& file_path) {
static const std::unordered_map<std::string, std::string> mime_types = {
// 텍스트
{”.html”, “text/html; charset=utf-8”},
{”.htm”,  “text/html; charset=utf-8”},
{”.css”,  “text/css; charset=utf-8”},
{”.js”,   “application/javascript; charset=utf-8”},
{”.txt”,  “text/plain; charset=utf-8”},
{”.xml”,  “application/xml; charset=utf-8”},

```
    // 이미지
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png",  "image/png"},
    {".gif",  "image/gif"},
    {".bmp",  "image/bmp"},
    {".svg",  "image/svg+xml"},
    {".webp", "image/webp"},
    
    // 오디오/비디오
    {".mp3",  "audio/mpeg"},
    {".mp4",  "video/mp4"},
    {".wav",  "audio/wav"},
    {".avi",  "video/x-msvideo"},
    
    // 문서
    {".pdf",  "application/pdf"},
    {".doc",  "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    
    // 압축
    {".zip",  "application/zip"},
    {".tar",  "application/x-tar"},
    {".gz",   "application/gzip"},
    
    // 기타
    {".json", "application/json; charset=utf-8"},
    {".ico",  "image/x-icon"}
};

// 파일 확장자 추출
auto extension = std::filesystem::path(file_path).extension().string();
std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

auto it = mime_types.find(extension);
if (it != mime_types.end()) {
    return it->second;
}

// 기본값
return "application/octet-stream";
```

}

void Response::update_content_length() {
with_header(“Content-Length”, std::to_string(body_.size()));
}

} // namespace stellane
