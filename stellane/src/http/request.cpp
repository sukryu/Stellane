#include “stellane/http/request.h”
#include <algorithm>
#include <sstream>
#include <regex>
#include <cctype>

namespace stellane {

// ============================================================================
// Request 구현
// ============================================================================

Request::Request(std::string method,
std::string path,
std::string query_string,
std::unordered_map<std::string, std::string> headers,
std::string body)
: method_(std::move(method))
, path_(std::move(path))
, query_string_(std::move(query_string))
, body_(std::move(body))
, headers_(std::move(headers)) {

```
// 헤더 이름 정규화
std::unordered_map<std::string, std::string> normalized_headers;
for (auto& [key, value] : headers_) {
    normalized_headers[normalize_header_name(key)] = value;
}
headers_ = std::move(normalized_headers);
```

}

// ========================================================================
// 기본 HTTP 정보 접근
// ========================================================================

const std::string& Request::method() const noexcept {
return method_;
}

const std::string& Request::path() const noexcept {
return path_;
}

const std::string& Request::query_string() const noexcept {
return query_string_;
}

const std::string& Request::body() const noexcept {
return body_;
}

size_t Request::content_length() const noexcept {
return body_.size();
}

bool Request::has_body() const noexcept {
return !body_.empty();
}

// ========================================================================
// HTTP 헤더 접근
// ========================================================================

std::optional<std::string> Request::header(const std::string& key) const {
auto normalized_key = normalize_header_name(key);
auto it = headers_.find(normalized_key);
if (it != headers_.end()) {
return it->second;
}
return std::nullopt;
}

const std::unordered_map<std::string, std::string>& Request::headers() const noexcept {
return headers_;
}

bool Request::has_header(const std::string& key) const {
return headers_.find(normalize_header_name(key)) != headers_.end();
}

std::optional<std::string> Request::content_type() const {
return header(“content-type”);
}

std::optional<std::string> Request::user_agent() const {
return header(“user-agent”);
}

std::optional<std::string> Request::authorization() const {
return header(“authorization”);
}

// ========================================================================
// 경로 파라미터 처리
// ========================================================================

void Request::set_path_params(std::unordered_map<std::string, std::string> params) {
path_params_ = std::move(params);
}

std::optional<std::string> Request::path_param(const std::string& key) const {
auto it = path_params_.find(key);
if (it != path_params_.end()) {
return it->second;
}
return std::nullopt;
}

const std::unordered_map<std::string, std::string>& Request::path_params() const noexcept {
return path_params_;
}

// ========================================================================
// 쿼리 파라미터 처리
// ========================================================================

std::optional<std::string> Request::query_param(const std::string& key) const {
const auto& params = query_params();
auto it = params.find(key);
if (it != params.end() && !it->second.empty()) {
return it->second.front();
}
return std::nullopt;
}

std::vector<std::string> Request::query_param_list(const std::string& key) const {
const auto& params = query_params();
auto it = params.find(key);
if (it != params.end()) {
return it->second;
}
return {};
}

const std::unordered_map<std::string, std::vector<std::string>>& Request::query_params() const {
if (!parsed_query_params_) {
parse_query_params();
}
return *parsed_query_params_;
}

void Request::parse_query_params() const {
parsed_query_params_ = std::unordered_map<std::string, std::vector<std::string>>{};

```
if (query_string_.empty()) {
    return;
}

std::istringstream iss(query_string_);
std::string pair;

while (std::getline(iss, pair, '&')) {
    auto eq_pos = pair.find('=');
    if (eq_pos != std::string::npos) {
        std::string key = url_decode(pair.substr(0, eq_pos));
        std::string value = url_decode(pair.substr(eq_pos + 1));
        
        (*parsed_query_params_)[key].push_back(value);
    } else {
        // 값 없는 파라미터 (예: ?debug)
        std::string key = url_decode(pair);
        (*parsed_query_params_)[key].push_back("");
    }
}
```

}

// ========================================================================
// JSON 본문 처리
// ========================================================================

std::optional<std::any> Request::json_body() const {
if (!is_json()) {
return std::nullopt;
}

```
parse_json();
return parsed_json_;
```

}

bool Request::is_json() const {
auto ct = content_type();
if (!ct) return false;

```
std::string content_type_lower = *ct;
std::transform(content_type_lower.begin(), content_type_lower.end(), 
              content_type_lower.begin(), ::tolower);

return content_type_lower.find("application/json") != std::string::npos;
```

}

void Request::parse_json() const {
if (parsed_json_) return;

```
try {
    // 실제 구현에서는 nlohmann::json 사용
    // auto json = nlohmann::json::parse(body_);
    // parsed_json_ = json;
    
    // 현재는 플레이스홀더 - 빈 JSON 객체
    parsed_json_ = std::any{};
} catch (...) {
    parsed_json_ = std::nullopt;
}
```

}

// ========================================================================
// Form 데이터 처리
// ========================================================================

const std::unordered_map<std::string, std::string>& Request::form_data() const {
if (!parsed_form_data_) {
parse_form_data();
}
return *parsed_form_data_;
}

std::optional<std::string> Request::form_field(const std::string& key) const {
const auto& data = form_data();
auto it = data.find(key);
if (it != data.end()) {
return it->second;
}
return std::nullopt;
}

bool Request::is_form_data() const {
auto ct = content_type();
if (!ct) return false;

```
return ct->find("application/x-www-form-urlencoded") != std::string::npos;
```

}

void Request::parse_form_data() const {
parsed_form_data_ = std::unordered_map<std::string, std::string>{};

```
if (!is_form_data()) return;

std::istringstream iss(body_);
std::string pair;

while (std::getline(iss, pair, '&')) {
    auto eq_pos = pair.find('=');
    if (eq_pos != std::string::npos) {
        std::string key = url_decode(pair.substr(0, eq_pos));
        std::string value = url_decode(pair.substr(eq_pos + 1));
        (*parsed_form_data_)[key] = value;
    }
}
```

}

// ========================================================================
// 멀티파트 데이터 처리
// ========================================================================

const std::vector<Request::MultipartFile>& Request::uploaded_files() const {
if (!parsed_multipart_files_) {
parse_multipart_data();
}
return *parsed_multipart_files_;
}

std::optional<Request::MultipartFile> Request::uploaded_file(const std::string& field_name) const {
const auto& files = uploaded_files();
for (const auto& file : files) {
if (file.field_name == field_name) {
return file;
}
}
return std::nullopt;
}

bool Request::is_multipart() const {
auto ct = content_type();
if (!ct) return false;

```
return ct->find("multipart/form-data") != std::string::npos;
```

}

void Request::parse_multipart_data() const {
parsed_multipart_files_ = std::vector<MultipartFile>{};

```
if (!is_multipart()) return;

// 멀티파트 파싱은 매우 복잡하므로 여기서는 기본 구현만
// 실제로는 전용 멀티파트 파서 라이브러리 사용 권장

auto ct = content_type();
if (!ct) return;

// boundary 추출
std::regex boundary_regex(R"(boundary=([^;]+))");
std::smatch matches;
if (!std::regex_search(*ct, matches, boundary_regex)) {
    return;
}

std::string boundary = "--" + matches[1].str();

// 간단한 멀티파트 파싱 (실제로는 더 복잡함)
size_t pos = 0;
while ((pos = body_.find(boundary, pos)) != std::string::npos) {
    pos += boundary.length();
    
    // Content-Disposition 헤더 찾기
    size_t header_end = body_.find("\r\n\r\n", pos);
    if (header_end == std::string::npos) break;
    
    std::string headers = body_.substr(pos, header_end - pos);
    pos = header_end + 4;
    
    // 다음 boundary 찾기
    size_t next_boundary = body_.find(boundary, pos);
    if (next_boundary == std::string::npos) break;
    
    std::string content = body_.substr(pos, next_boundary - pos - 2); // -2 for \r\n
    
    // Content-Disposition 파싱
    std::regex name_regex(R"(name="([^"]+)")");
    std::regex filename_regex(R"(filename="([^"]+)")");
    std::smatch name_match, filename_match;
    
    if (std::regex_search(headers, name_match, name_regex)) {
        MultipartFile file;
        file.field_name = name_match[1].str();
        file.content = content;
        file.size = content.length();
        
        if (std::regex_search(headers, filename_match, filename_regex)) {
            file.filename = filename_match[1].str();
            
            // Content-Type 추출 (간단화)
            std::regex ct_regex(R"(Content-Type:\s*([^\r\n]+))");
            std::smatch ct_match;
            if (std::regex_search(headers, ct_match, ct_regex)) {
                file.content_type = ct_match[1].str();
            } else {
                file.content_type = "application/octet-stream";
            }
        }
        
        parsed_multipart_files_->push_back(std::move(file));
    }
    
    pos = next_boundary;
}
```

}

// ========================================================================
// 쿠키 처리
// ========================================================================

std::optional<std::string> Request::cookie(const std::string& name) const {
const auto& cookie_map = cookies();
auto it = cookie_map.find(name);
if (it != cookie_map.end()) {
return it->second;
}
return std::nullopt;
}

const std::unordered_map<std::string, std::string>& Request::cookies() const {
if (!parsed_cookies_) {
parse_cookies();
}
return *parsed_cookies_;
}

void Request::parse_cookies() const {
parsed_cookies_ = std::unordered_map<std::string, std::string>{};

```
auto cookie_header = header("cookie");
if (!cookie_header) return;

std::istringstream iss(*cookie_header);
std::string cookie_pair;

while (std::getline(iss, cookie_pair, ';')) {
    // 앞뒤 공백 제거
    cookie_pair.erase(0, cookie_pair.find_first_not_of(" \t"));
    cookie_pair.erase(cookie_pair.find_last_not_of(" \t") + 1);
    
    auto eq_pos = cookie_pair.find('=');
    if (eq_pos != std::string::npos) {
        std::string name = cookie_pair.substr(0, eq_pos);
        std::string value = cookie_pair.substr(eq_pos + 1);
        (*parsed_cookies_)[name] = value;
    }
}
```

}

// ========================================================================
// 유틸리티 메서드
// ========================================================================

std::string Request::client_ip() const {
// X-Forwarded-For 헤더 우선 확인 (프록시/로드밸런서 환경)
auto forwarded_for = header(“x-forwarded-for”);
if (forwarded_for) {
// 첫 번째 IP 주소 추출 (여러 개일 수 있음)
auto comma_pos = forwarded_for->find(’,’);
if (comma_pos != std::string::npos) {
return forwarded_for->substr(0, comma_pos);
}
return *forwarded_for;
}

```
// X-Real-IP 헤더 확인
auto real_ip = header("x-real-ip");
if (real_ip) {
    return *real_ip;
}

// 기본적으로는 직접 연결된 클라이언트 IP
// 실제 구현에서는 소켓에서 가져와야 함
return "127.0.0.1"; // 플레이스홀더
```

}

bool Request::is_secure() const {
// X-Forwarded-Proto 헤더 확인 (프록시 환경)
auto forwarded_proto = header(“x-forwarded-proto”);
if (forwarded_proto && *forwarded_proto == “https”) {
return true;
}

```
// X-Forwarded-SSL 헤더 확인
auto forwarded_ssl = header("x-forwarded-ssl");
if (forwarded_ssl && *forwarded_ssl == "on") {
    return true;
}

// 실제로는 TLS 연결 여부를 소켓에서 확인해야 함
return false; // 플레이스홀더
```

}

bool Request::is_ajax() const {
auto requested_with = header(“x-requested-with”);
return requested_with && *requested_with == “XMLHttpRequest”;
}

std::optional<std::string> Request::best_accept_type(
const std::vector<std::string>& available_types) const {

```
auto accept_header = header("accept");
if (!accept_header) {
    return available_types.empty() ? std::nullopt : 
                                    std::make_optional(available_types.front());
}

// Accept 헤더 파싱 (간단화된 버전)
// 실제로는 quality values (q=0.8) 등을 고려해야 함
std::string accept_lower = *accept_header;
std::transform(accept_lower.begin(), accept_lower.end(), 
              accept_lower.begin(), ::tolower);

for (const auto& type : available_types) {
    std::string type_lower = type;
    std::transform(type_lower.begin(), type_lower.end(), 
                  type_lower.begin(), ::tolower);
    
    if (accept_lower.find(type_lower) != std::string::npos) {
        return type;
    }
}

// 와일드카드 지원
if (accept_lower.find("*/*") != std::string::npos && !available_types.empty()) {
    return available_types.front();
}

return std::nullopt;
```

}

bool Request::exceeds_size_limit(size_t max_size_bytes) const {
return content_length() > max_size_bytes;
}

// ========================================================================
// 디버그 및 로깅
// ========================================================================

std::string Request::to_string(bool include_body) const {
std::ostringstream oss;

```
// 요청 라인
oss << method_ << " " << path_;
if (!query_string_.empty()) {
    oss << "?" << query_string_;
}
oss << " HTTP/1.1\n";

// 헤더들
for (const auto& [key, value] : headers_) {
    oss << key << ": " << value << "\n";
}

// 본문 (선택적)
if (include_body && has_body()) {
    oss << "\n" << body_;
}

return oss.str();
```

}

std::string Request::summary() const {
std::ostringstream oss;
oss << method_ << “ “ << path_;
if (!query_string_.empty()) {
oss << “?” << query_string_;
}
return oss.str();
}

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

std::string Request::url_decode(const std::string& encoded) {
std::string decoded;
decoded.reserve(encoded.length());

```
for (size_t i = 0; i < encoded.length(); ++i) {
    if (encoded[i] == '%' && i + 2 < encoded.length()) {
        // 16진수 디코딩
        char hex_str[3] = {encoded[i + 1], encoded[i + 2], '\0'};
        char* end_ptr;
        long value = std::strtol(hex_str, &end_ptr, 16);
        
        if (end_ptr == hex_str + 2) {
            decoded += static_cast<char>(value);
            i += 2;
        } else {
            decoded += encoded[i];
        }
    } else if (encoded[i] == '+') {
        // 공백 처리 (form data에서)
        decoded += ' ';
    } else {
        decoded += encoded[i];
    }
}

return decoded;
```

}

std::string Request::normalize_header_name(const std::string& header_name) {
std::string normalized = header_name;
std::transform(normalized.begin(), normalized.end(),
normalized.begin(), ::tolower);
return normalized;
}

} // namespace stellane
