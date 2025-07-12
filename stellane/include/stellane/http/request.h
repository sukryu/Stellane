#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <memory>
#include <any>

namespace stellane {

// Forward declarations
class Context;

/**

- @brief HTTP 요청을 표현하는 클래스
- 
- Stellane의 모든 요청 처리는 이 Request 객체를 중심으로 이루어집니다.
- 헤더, 경로 파라미터, 쿼리 스트링, 본문 등 HTTP 요청의 모든 정보를 포함합니다.
- 
- 특징:
- - 타입 안전한 파라미터 추출
- - JSON 본문 자동 파싱
- - 지연 파싱 (성능 최적화)
- - 불변 객체 설계
- 
- @example
- ```cpp
  
  ```
- // GET /users/123?limit=10
- auto user_id = req.path_param<int>(“id”);        // 123
- auto limit = req.query_param<int>(“limit”);      // 10
- auto auth = req.header(“Authorization”);         // “Bearer token…”
- 
- // POST /users with JSON body
- auto user_data = req.parse_json_body<UserDto>();
- ```
  
  ```

*/
class Request {
public:
/**
* @brief Request 생성자 (프레임워크 내부용)
* @param method HTTP 메서드 (GET, POST, PUT, DELETE, …)
* @param path 정규화된 경로 (/users/123)
* @param query_string URL 쿼리 스트링 (?key=value&…)
* @param headers HTTP 헤더 맵
* @param body 요청 본문 (raw bytes)
*/
Request(std::string method,
std::string path,
std::string query_string,
std::unordered_map<std::string, std::string> headers,
std::string body);


/**
 * @brief 복사 생성자
 */
Request(const Request& other) = default;
Request& operator=(const Request& other) = default;

/**
 * @brief 이동 생성자
 */
Request(Request&& other) = default;
Request& operator=(Request&& other) = default;

/**
 * @brief 소멸자
 */
~Request() = default;

// ========================================================================
// 기본 HTTP 정보 접근
// ========================================================================

/**
 * @brief HTTP 메서드 반환
 * @return 메서드 문자열 (GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD)
 */
[[nodiscard]] const std::string& method() const noexcept;

/**
 * @brief 정규화된 경로 반환
 * @return 경로 문자열 (예: /users/123)
 */
[[nodiscard]] const std::string& path() const noexcept;

/**
 * @brief 원본 쿼리 스트링 반환
 * @return 쿼리 스트링 (예: page=2&limit=10)
 */
[[nodiscard]] const std::string& query_string() const noexcept;

/**
 * @brief 요청 본문 반환
 * @return 원시 본문 데이터
 */
[[nodiscard]] const std::string& body() const noexcept;

/**
 * @brief Content-Length 반환
 * @return 본문 크기 (바이트)
 */
[[nodiscard]] size_t content_length() const noexcept;

/**
 * @brief 요청에 본문이 있는지 확인
 * @return 본문 존재 여부
 */
[[nodiscard]] bool has_body() const noexcept;

// ========================================================================
// HTTP 헤더 접근
// ========================================================================

/**
 * @brief 특정 헤더 값 조회
 * @param key 헤더 이름 (대소문자 무시)
 * @return 헤더 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> header(const std::string& key) const;

/**
 * @brief 모든 헤더 반환
 * @return 헤더 맵 (key: 소문자 정규화, value: 원본)
 */
[[nodiscard]] const std::unordered_map<std::string, std::string>& headers() const noexcept;

/**
 * @brief 특정 헤더 존재 여부 확인
 * @param key 헤더 이름
 * @return 존재 여부
 */
[[nodiscard]] bool has_header(const std::string& key) const;

/**
 * @brief Content-Type 헤더 반환
 * @return MIME 타입 (예: application/json)
 */
[[nodiscard]] std::optional<std::string> content_type() const;

/**
 * @brief User-Agent 헤더 반환
 * @return 사용자 에이전트 문자열
 */
[[nodiscard]] std::optional<std::string> user_agent() const;

/**
 * @brief Authorization 헤더 반환
 * @return 인증 헤더 (예: Bearer token...)
 */
[[nodiscard]] std::optional<std::string> authorization() const;

// ========================================================================
// 경로 파라미터 처리
// ========================================================================

/**
 * @brief 경로 파라미터 설정 (프레임워크 내부용)
 * @param params 라우터가 추출한 파라미터 맵
 */
void set_path_params(std::unordered_map<std::string, std::string> params);

/**
 * @brief 경로 파라미터 조회
 * @param key 파라미터 이름 (예: "id", "slug")
 * @return 파라미터 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> path_param(const std::string& key) const;

/**
 * @brief 타입 안전한 경로 파라미터 조회
 * @tparam T 변환할 타입 (int, long, double, std::string)
 * @param key 파라미터 이름
 * @return 변환된 값 (실패 시 std::nullopt)
 */
template<typename T>
[[nodiscard]] std::optional<T> path_param(const std::string& key) const;

/**
 * @brief 모든 경로 파라미터 반환
 * @return 파라미터 맵
 */
[[nodiscard]] const std::unordered_map<std::string, std::string>& path_params() const noexcept;

// ========================================================================
// 쿼리 파라미터 처리
// ========================================================================

/**
 * @brief 쿼리 파라미터 조회
 * @param key 파라미터 이름
 * @return 파라미터 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> query_param(const std::string& key) const;

/**
 * @brief 타입 안전한 쿼리 파라미터 조회
 * @tparam T 변환할 타입
 * @param key 파라미터 이름
 * @return 변환된 값 (실패 시 std::nullopt)
 */
template<typename T>
[[nodiscard]] std::optional<T> query_param(const std::string& key) const;

/**
 * @brief 배열 형태 쿼리 파라미터 조회
 * @param key 파라미터 이름 (예: "tags")
 * @return 값 목록 (예: ?tags=a&tags=b → ["a", "b"])
 */
[[nodiscard]] std::vector<std::string> query_param_list(const std::string& key) const;

/**
 * @brief 모든 쿼리 파라미터 반환
 * @return 파라미터 맵 (지연 파싱)
 */
[[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>>& query_params() const;

// ========================================================================
// JSON 본문 처리
// ========================================================================

/**
 * @brief JSON 본문을 지정된 타입으로 파싱
 * @tparam T 디시리얼라이즈할 타입 (DTO 클래스)
 * @return 파싱된 객체 (실패 시 std::nullopt)
 * 
 * @example
 * ```cpp
 * struct CreateUserDto {
 *     std::string name;
 *     std::string email;
 *     int age;
 * };
 * 
 * auto user_data = req.parse_json_body<CreateUserDto>();
 * if (user_data) {
 *     // 사용 가능
 * }
 * ```
 */
template<typename T>
[[nodiscard]] std::optional<T> parse_json_body() const;

/**
 * @brief 원시 JSON 객체 반환
 * @return JSON 객체 (nlohmann::json 또는 유사)
 */
[[nodiscard]] std::optional<std::any> json_body() const;

/**
 * @brief JSON 유효성 검사
 * @return JSON 형식 여부
 */
[[nodiscard]] bool is_json() const;

// ========================================================================
// Form 데이터 처리
// ========================================================================

/**
 * @brief application/x-www-form-urlencoded 데이터 파싱
 * @return 폼 필드 맵
 */
[[nodiscard]] const std::unordered_map<std::string, std::string>& form_data() const;

/**
 * @brief 폼 필드 조회
 * @param key 필드 이름
 * @return 필드 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> form_field(const std::string& key) const;

/**
 * @brief 요청이 폼 데이터인지 확인
 * @return 폼 데이터 여부
 */
[[nodiscard]] bool is_form_data() const;

// ========================================================================
// 멀티파트 데이터 처리 (파일 업로드)
// ========================================================================

struct MultipartFile {
    std::string field_name;      ///< 폼 필드 이름
    std::string filename;        ///< 원본 파일명
    std::string content_type;    ///< MIME 타입
    std::string content;         ///< 파일 내용
    size_t size;                 ///< 파일 크기
};

/**
 * @brief 업로드된 파일 목록 반환
 * @return 업로드 파일 목록
 */
[[nodiscard]] const std::vector<MultipartFile>& uploaded_files() const;

/**
 * @brief 특정 필드의 업로드 파일 조회
 * @param field_name 폼 필드 이름
 * @return 파일 정보 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<MultipartFile> uploaded_file(const std::string& field_name) const;

/**
 * @brief 멀티파트 요청인지 확인
 * @return 멀티파트 여부
 */
[[nodiscard]] bool is_multipart() const;

// ========================================================================
// 쿠키 처리
// ========================================================================

/**
 * @brief 쿠키 값 조회
 * @param name 쿠키 이름
 * @return 쿠키 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> cookie(const std::string& name) const;

/**
 * @brief 모든 쿠키 반환
 * @return 쿠키 맵
 */
[[nodiscard]] const std::unordered_map<std::string, std::string>& cookies() const;

// ========================================================================
// 유틸리티 메서드
// ========================================================================

/**
 * @brief 클라이언트 IP 주소 추출
 * @return IP 주소 (X-Forwarded-For, X-Real-IP 헤더 고려)
 */
[[nodiscard]] std::string client_ip() const;

/**
 * @brief 요청이 HTTPS인지 확인
 * @return HTTPS 여부
 */
[[nodiscard]] bool is_secure() const;

/**
 * @brief 요청이 AJAX인지 확인
 * @return AJAX 여부 (X-Requested-With: XMLHttpRequest)
 */
[[nodiscard]] bool is_ajax() const;

/**
 * @brief Accept 헤더 기반 컨텐츠 타입 협상
 * @param available_types 서버가 제공 가능한 타입들
 * @return 최적 매치 타입 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> best_accept_type(
    const std::vector<std::string>& available_types) const;

/**
 * @brief 요청 크기 제한 검사
 * @param max_size_bytes 최대 허용 크기
 * @return 크기 초과 여부
 */
[[nodiscard]] bool exceeds_size_limit(size_t max_size_bytes) const;

// ========================================================================
// 디버그 및 로깅
// ========================================================================

/**
 * @brief 요청 정보를 문자열로 변환 (디버그용)
 * @param include_body 본문 포함 여부
 * @return 포맷된 요청 정보
 */
[[nodiscard]] std::string to_string(bool include_body = false) const;

/**
 * @brief 요청 요약 정보 반환
 * @return "GET /users/123" 형태의 요약
 */
[[nodiscard]] std::string summary() const;
```

private:
// ========================================================================
// 내부 데이터
// ========================================================================

```
std::string method_;
std::string path_;
std::string query_string_;
std::string body_;
std::unordered_map<std::string, std::string> headers_;
std::unordered_map<std::string, std::string> path_params_;

// 지연 파싱 캐시
mutable std::optional<std::unordered_map<std::string, std::vector<std::string>>> parsed_query_params_;
mutable std::optional<std::unordered_map<std::string, std::string>> parsed_form_data_;
mutable std::optional<std::vector<MultipartFile>> parsed_multipart_files_;
mutable std::optional<std::unordered_map<std::string, std::string>> parsed_cookies_;
mutable std::optional<std::any> parsed_json_;

// ========================================================================
// 내부 파싱 메서드
// ========================================================================

/**
 * @brief 쿼리 스트링 파싱 (지연 실행)
 */
void parse_query_params() const;

/**
 * @brief 폼 데이터 파싱 (지연 실행)
 */
void parse_form_data() const;

/**
 * @brief 멀티파트 데이터 파싱 (지연 실행)
 */
void parse_multipart_data() const;

/**
 * @brief 쿠키 파싱 (지연 실행)
 */
void parse_cookies() const;

/**
 * @brief JSON 파싱 (지연 실행)
 */
void parse_json() const;

/**
 * @brief URL 디코딩
 * @param encoded 인코딩된 문자열
 * @return 디코딩된 문자열
 */
static std::string url_decode(const std::string& encoded);

/**
 * @brief 헤더 이름 정규화 (소문자 변환)
 * @param header_name 원본 헤더 이름
 * @return 정규화된 헤더 이름
 */
static std::string normalize_header_name(const std::string& header_name);

/**
 * @brief 타입 변환 헬퍼
 * @tparam T 변환할 타입
 * @param value 원본 문자열 값
 * @return 변환된 값 (실패 시 std::nullopt)
 */
template<typename T>
static std::optional<T> convert_string_to(const std::string& value);
```

};

// ============================================================================
// 템플릿 구현
// ============================================================================

template<typename T>
std::optional<T> Request::path_param(const std::string& key) const {
auto param = path_param(key);
if (!param) {
return std::nullopt;
}

```
return convert_string_to<T>(*param);
```

}

template<typename T>
std::optional<T> Request::query_param(const std::string& key) const {
auto param = query_param(key);
if (!param) {
return std::nullopt;
}

```
return convert_string_to<T>(*param);
```

}

template<typename T>
std::optional<T> Request::parse_json_body() const {
if (!is_json()) {
return std::nullopt;
}

```
try {
    parse_json();
    if (!parsed_json_) {
        return std::nullopt;
    }
    
    // JSON 디시리얼라이제이션 로직
    // 실제 구현에서는 nlohmann::json 또는 유사 라이브러리 사용
    // return nlohmann::json::parse(body_).get<T>();
    
    // 현재는 플레이스홀더
    return std::nullopt;
} catch (...) {
    return std::nullopt;
}
```

}

template<typename T>
std::optional<T> Request::convert_string_to(const std::string& value) {
if constexpr (std::is_same_v<T, std::string>) {
return value;
} else if constexpr (std::is_same_v<T, int>) {
try {
return std::stoi(value);
} catch (…) {
return std::nullopt;
}
} else if constexpr (std::is_same_v<T, long>) {
try {
return std::stol(value);
} catch (…) {
return std::nullopt;
}
} else if constexpr (std::is_same_v<T, long long>) {
try {
return std::stoll(value);
} catch (…) {
return std::nullopt;
}
} else if constexpr (std::is_same_v<T, double>) {
try {
return std::stod(value);
} catch (…) {
return std::nullopt;
}
} else if constexpr (std::is_same_v<T, float>) {
try {
return std::stof(value);
} catch (…) {
return std::nullopt;
}
} else if constexpr (std::is_same_v<T, bool>) {
std::string lower_value = value;
std::transform(lower_value.begin(), lower_value.end(),
lower_value.begin(), ::tolower);

```
    if (lower_value == "true" || lower_value == "1" || lower_value == "yes") {
        return true;
    } else if (lower_value == "false" || lower_value == "0" || lower_value == "no") {
        return false;
    } else {
        return std::nullopt;
    }
} else {
    static_assert(false, "Unsupported type for parameter conversion");
}
```

}

} // namespace stellane
