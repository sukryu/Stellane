#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>
#include <any>

namespace stellane {

/**

- @brief HTTP 응답을 표현하는 클래스
- 
- Stellane의 모든 핸들러는 Response 객체를 반환합니다.
- Fluent Interface 패턴을 사용하여 체이닝 방식으로 응답을 구성할 수 있습니다.
- 
- 특징:
- - 메서드 체이닝 지원
- - 자동 Content-Length 계산
- - 타입 안전한 JSON 직렬화
- - 표준 HTTP 상태 코드 지원
- - 쿠키 설정 지원
- 
- @example
- ```cpp
  
  ```
- // 간단한 텍스트 응답
- return Response::ok(“Hello, World!”);
- 
- // JSON 응답 with 헤더
- return Response::ok(user.to_json())
- ```
        .with_content_type("application/json")
  ```
- ```
        .with_header("X-Custom", "value");
  ```
- 
- // 리다이렉트
- return Response::redirect(”/dashboard”);
- 
- // 에러 응답
- return Response::internal_error(“Database connection failed”);
- ```
  
  ```

*/
class Response {
public:
/**
* @brief 쿠키 설정 옵션
*/
struct CookieOptions {
std::optional<std::chrono::seconds> max_age;    ///< 만료 시간
std::optional<std::string> domain;              ///< 도메인
std::optional<std::string> path;                ///< 경로
bool secure = false;                            ///< HTTPS 전용
bool http_only = false;                         ///< JavaScript 접근 차단
std::optional<std::string> same_site;           ///< SameSite 정책
};

```
/**
 * @brief 기본 생성자 (200 OK)
 */
Response();

/**
 * @brief 상태 코드와 본문으로 생성
 * @param status_code HTTP 상태 코드
 * @param body 응답 본문
 */
explicit Response(int status_code, std::string body = "");

/**
 * @brief 복사 생성자
 */
Response(const Response& other) = default;
Response& operator=(const Response& other) = default;

/**
 * @brief 이동 생성자
 */
Response(Response&& other) = default;
Response& operator=(Response&& other) = default;

/**
 * @brief 소멸자
 */
~Response() = default;

// ========================================================================
// 정적 팩토리 메서드 (2xx 성공)
// ========================================================================

/**
 * @brief 200 OK 응답 생성
 * @param body 응답 본문
 * @return Response 객체
 */
static Response ok(std::string body = "");

/**
 * @brief 201 Created 응답 생성
 * @param body 응답 본문
 * @return Response 객체
 */
static Response created(std::string body = "");

/**
 * @brief 202 Accepted 응답 생성
 * @param body 응답 본문
 * @return Response 객체
 */
static Response accepted(std::string body = "");

/**
 * @brief 204 No Content 응답 생성
 * @return Response 객체
 */
static Response no_content();

// ========================================================================
// 정적 팩토리 메서드 (3xx 리다이렉트)
// ========================================================================

/**
 * @brief 301 Moved Permanently 응답 생성
 * @param location 리다이렉트 URL
 * @return Response 객체
 */
static Response moved_permanently(const std::string& location);

/**
 * @brief 302 Found (임시 리다이렉트) 응답 생성
 * @param location 리다이렉트 URL
 * @return Response 객체
 */
static Response found(const std::string& location);

/**
 * @brief 304 Not Modified 응답 생성
 * @return Response 객체
 */
static Response not_modified();

/**
 * @brief 일반적인 리다이렉트 (302 Found)
 * @param location 리다이렉트 URL
 * @return Response 객체
 */
static Response redirect(const std::string& location);

// ========================================================================
// 정적 팩토리 메서드 (4xx 클라이언트 오류)
// ========================================================================

/**
 * @brief 400 Bad Request 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response bad_request(std::string message = "Bad Request");

/**
 * @brief 401 Unauthorized 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response unauthorized(std::string message = "Unauthorized");

/**
 * @brief 403 Forbidden 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response forbidden(std::string message = "Forbidden");

/**
 * @brief 404 Not Found 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response not_found(std::string message = "Not Found");

/**
 * @brief 405 Method Not Allowed 응답 생성
 * @param allowed_methods 허용된 메서드 목록
 * @return Response 객체
 */
static Response method_not_allowed(const std::vector<std::string>& allowed_methods = {});

/**
 * @brief 409 Conflict 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response conflict(std::string message = "Conflict");

/**
 * @brief 422 Unprocessable Entity 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response unprocessable_entity(std::string message = "Unprocessable Entity");

/**
 * @brief 429 Too Many Requests 응답 생성
 * @param retry_after 재시도 대기 시간 (초)
 * @return Response 객체
 */
static Response too_many_requests(std::optional<int> retry_after = std::nullopt);

// ========================================================================
// 정적 팩토리 메서드 (5xx 서버 오류)
// ========================================================================

/**
 * @brief 500 Internal Server Error 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response internal_error(std::string message = "Internal Server Error");

/**
 * @brief 501 Not Implemented 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response not_implemented(std::string message = "Not Implemented");

/**
 * @brief 502 Bad Gateway 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response bad_gateway(std::string message = "Bad Gateway");

/**
 * @brief 503 Service Unavailable 응답 생성
 * @param message 오류 메시지
 * @return Response 객체
 */
static Response service_unavailable(std::string message = "Service Unavailable");

// ========================================================================
// 상태 및 본문 설정 (Fluent Interface)
// ========================================================================

/**
 * @brief 상태 코드 설정
 * @param code HTTP 상태 코드
 * @return Response& (체이닝)
 */
Response& with_status(int code);

/**
 * @brief 응답 본문 설정
 * @param body 본문 내용
 * @return Response& (체이닝)
 */
Response& with_body(std::string body);

/**
 * @brief JSON 응답 설정
 * @tparam T JSON 직렬화 가능한 타입
 * @param data 직렬화할 객체
 * @return Response& (체이닝)
 */
template<typename T>
Response& with_json(const T& data);

/**
 * @brief 파일 내용으로 응답 설정
 * @param file_path 파일 경로
 * @param content_type MIME 타입 (자동 감지 시 빈 문자열)
 * @return Response& (체이닝)
 */
Response& with_file(const std::string& file_path, const std::string& content_type = "");

// ========================================================================
// 헤더 설정
// ========================================================================

/**
 * @brief HTTP 헤더 추가/수정
 * @param key 헤더 이름
 * @param value 헤더 값
 * @return Response& (체이닝)
 */
Response& with_header(const std::string& key, const std::string& value);

/**
 * @brief 여러 헤더 한번에 설정
 * @param headers 헤더 맵
 * @return Response& (체이닝)
 */
Response& with_headers(const std::unordered_map<std::string, std::string>& headers);

/**
 * @brief Content-Type 헤더 설정
 * @param content_type MIME 타입
 * @return Response& (체이닝)
 */
Response& with_content_type(const std::string& content_type);

/**
 * @brief Cache-Control 헤더 설정
 * @param cache_control 캐시 제어 지시어
 * @return Response& (체이닝)
 */
Response& with_cache_control(const std::string& cache_control);

/**
 * @brief CORS 헤더 설정
 * @param origin 허용할 오리진 (기본: *)
 * @param methods 허용할 메서드들
 * @param headers 허용할 헤더들
 * @return Response& (체이닝)
 */
Response& with_cors(const std::string& origin = "*",
                   const std::vector<std::string>& methods = {"GET", "POST", "PUT", "DELETE"},
                   const std::vector<std::string>& headers = {"Content-Type", "Authorization"});

// ========================================================================
// 쿠키 설정
// ========================================================================

/**
 * @brief 쿠키 설정
 * @param name 쿠키 이름
 * @param value 쿠키 값
 * @param options 쿠키 옵션
 * @return Response& (체이닝)
 */
Response& with_cookie(const std::string& name, 
                     const std::string& value,
                     const CookieOptions& options = {});

/**
 * @brief 쿠키 삭제 (만료 시간을 과거로 설정)
 * @param name 삭제할 쿠키 이름
 * @return Response& (체이닝)
 */
Response& without_cookie(const std::string& name);

// ========================================================================
// 접근자 메서드
// ========================================================================

/**
 * @brief HTTP 상태 코드 반환
 * @return 상태 코드
 */
[[nodiscard]] int status_code() const noexcept;

/**
 * @brief 응답 본문 반환
 * @return 본문 내용
 */
[[nodiscard]] const std::string& body() const noexcept;

/**
 * @brief 응답 본문 크기 반환
 * @return 본문 크기 (바이트)
 */
[[nodiscard]] size_t content_length() const noexcept;

/**
 * @brief 모든 헤더 반환
 * @return 헤더 맵
 */
[[nodiscard]] const std::unordered_map<std::string, std::string>& headers() const noexcept;

/**
 * @brief 특정 헤더 값 조회
 * @param key 헤더 이름
 * @return 헤더 값 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<std::string> header(const std::string& key) const;

/**
 * @brief 헤더 존재 여부 확인
 * @param key 헤더 이름
 * @return 존재 여부
 */
[[nodiscard]] bool has_header(const std::string& key) const;

/**
 * @brief Content-Type 헤더 반환
 * @return MIME 타입
 */
[[nodiscard]] std::optional<std::string> content_type() const;

/**
 * @brief 설정된 쿠키 목록 반환
 * @return 쿠키 문자열 목록 (Set-Cookie 헤더 형식)
 */
[[nodiscard]] const std::vector<std::string>& cookies() const noexcept;

// ========================================================================
// 상태 확인 메서드
// ========================================================================

/**
 * @brief 성공 응답인지 확인 (2xx)
 * @return 성공 여부
 */
[[nodiscard]] bool is_success() const noexcept;

/**
 * @brief 리다이렉트 응답인지 확인 (3xx)
 * @return 리다이렉트 여부
 */
[[nodiscard]] bool is_redirect() const noexcept;

/**
 * @brief 클라이언트 오류 응답인지 확인 (4xx)
 * @return 클라이언트 오류 여부
 */
[[nodiscard]] bool is_client_error() const noexcept;

/**
 * @brief 서버 오류 응답인지 확인 (5xx)
 * @return 서버 오류 여부
 */
[[nodiscard]] bool is_server_error() const noexcept;

/**
 * @brief 오류 응답인지 확인 (4xx 또는 5xx)
 * @return 오류 여부
 */
[[nodiscard]] bool is_error() const noexcept;

/**
 * @brief 본문이 있는지 확인
 * @return 본문 존재 여부
 */
[[nodiscard]] bool has_body() const noexcept;

// ========================================================================
// 직렬화 및 디버그
// ========================================================================

/**
 * @brief HTTP 응답 메시지로 직렬화
 * @return HTTP 응답 문자열
 * 
 * @example
 * ```
 * HTTP/1.1 200 OK
 * Content-Type: application/json
 * Content-Length: 42
 * 
 * {"message": "Hello, World!"}
 * ```
 */
[[nodiscard]] std::string to_http_string() const;

/**
 * @brief 응답 요약 정보 반환 (디버그용)
 * @return "200 OK (42 bytes)" 형태의 요약
 */
[[nodiscard]] std::string summary() const;

/**
 * @brief 상태 코드의 표준 메시지 반환
 * @param status_code 상태 코드
 * @return 표준 상태 메시지 (예: 200 → "OK")
 */
[[nodiscard]] static std::string status_message(int status_code);
```

private:
// ========================================================================
// 내부 데이터
// ========================================================================

```
int status_code_;
std::string body_;
std::unordered_map<std::string, std::string> headers_;
std::vector<std::string> set_cookies_;

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

/**
 * @brief 헤더 이름 정규화 (대소문자 일관성)
 * @param header_name 원본 헤더 이름
 * @return 정규화된 헤더 이름
 */
static std::string normalize_header_name(const std::string& header_name);

/**
 * @brief 쿠키 옵션을 Set-Cookie 문자열로 변환
 * @param name 쿠키 이름
 * @param value 쿠키 값
 * @param options 쿠키 옵션
 * @return Set-Cookie 헤더 값
 */
static std::string format_set_cookie(const std::string& name,
                                    const std::string& value,
                                    const CookieOptions& options);

/**
 * @brief 파일 확장자로부터 MIME 타입 추론
 * @param file_path 파일 경로
 * @return 추론된 MIME 타입
 */
static std::string guess_content_type(const std::string& file_path);

/**
 * @brief Content-Length 헤더 자동 업데이트
 */
void update_content_length();
```

};

// ============================================================================
// 템플릿 구현
// ============================================================================

template<typename T>
Response& Response::with_json(const T& data) {
// 실제 구현에서는 nlohmann::json 또는 유사 라이브러리 사용
// nlohmann::json j = data;
// body_ = j.dump();

```
// 현재는 플레이스홀더
body_ = "{}"; // JSON 직렬화 결과

with_content_type("application/json");
update_content_length();

return *this;
```

}

// ============================================================================
// 편의 타입 별칭
// ============================================================================

/**

- @brief JSON 응답 생성 헬퍼
- @tparam T JSON 직렬화 가능한 타입
- @param data 직렬화할 객체
- @param status_code HTTP 상태 코드 (기본: 200)
- @return JSON 응답
  */
  template<typename T>
  Response json_response(const T& data, int status_code = 200) {
  return Response(status_code).with_json(data);
  }

/**

- @brief 텍스트 응답 생성 헬퍼
- @param text 텍스트 내용
- @param status_code HTTP 상태 코드 (기본: 200)
- @return 텍스트 응답
  */
  inline Response text_response(const std::string& text, int status_code = 200) {
  return Response(status_code, text).with_content_type(“text/plain; charset=utf-8”);
  }

/**

- @brief HTML 응답 생성 헬퍼
- @param html HTML 내용
- @param status_code HTTP 상태 코드 (기본: 200)
- @return HTML 응답
  */
  inline Response html_response(const std::string& html, int status_code = 200) {
  return Response(status_code, html).with_content_type(“text/html; charset=utf-8”);
  }

} // namespace stellane
