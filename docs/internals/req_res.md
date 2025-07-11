# Request & Response API Reference

Stellane의 모든 요청 처리 흐름은 `Request`와 `Response` 객체를 중심으로 이루어집니다.  
이 문서에서는 두 객체의 **전체 API**, **상태**, **헬퍼 함수**, **동작 방식**을 명세합니다.

---

## 📥 Request

### 1. 구조 및 기본 정보

```cpp
class Request {
public:
    std::string method() const;
    std::string path() const;
    std::string query_string() const;
    std::string header(const std::string& key) const;
    std::string body() const;

    // 고급 기능
    template<typename T>
    std::optional<T> parse_json_body() const;

    std::map<std::string, std::string> path_params() const;
    std::map<std::string, std::string> query_params() const;
};
```

⸻

### 2. 주요 필드 및 메서드

메서드	설명
method()	HTTP 메서드 (GET, POST, …)
path()	정규화된 경로 (/users/123)
query_string()	URL 뒤의 raw 쿼리 문자열 (?page=2&limit=10)
header(key)	특정 HTTP 헤더 값을 반환 (없으면 빈 문자열)
body()	요청의 원시 body 내용 (문자열)


⸻

### 3. 고급 파싱 기능

✅ parse_json_body<T>()
```cpp
struct LoginDto {
    std::string username;
    std::string password;
};

auto dto = req.parse_json_body<LoginDto>();
if (!dto) return Response::bad_request("Invalid JSON");
```
  •	내부적으로 JSON을 파싱하여 지정된 타입으로 역직렬화합니다.
	•	역직렬화 실패 시 std::nullopt 반환
	•	내부적으로 nlohmann::json 혹은 simdjson과 같은 JSON 파서 사용 가능

⸻

### 4. 쿼리 파라미터
```cpp
auto params = req.query_params();
auto page = params["page"]; // "1"
```
  •	모든 ?key=value&...를 map으로 변환
	•	자동 URL 디코딩 처리

⸻

### 5. 경로 파라미터
```cpp
// GET /users/:id → /users/42
auto id = req.path_params()["id"]; // "42"
```
  •	라우터가 자동 추출한 경로 파라미터
	•	정수/실수 변환은 직접 수행하거나 Context Injector 사용

⸻

## 📤 Response

### 1. 구조
```cpp
class Response {
public:
    static Response ok(std::string body);
    static Response created(std::string body);
    static Response not_found(std::string message);
    static Response bad_request(std::string message);
    static Response internal_server_error(std::string message);

    Response& with_header(std::string key, std::string value);
    Response& with_status(int code);
    Response& with_content_type(std::string mime);

    int status_code() const;
    std::string body() const;
    const std::map<std::string, std::string>& headers() const;
};
```

⸻

### 2. 주요 생성 메서드

메서드	설명
ok(body)	200 OK
created(body)	201 Created
not_found(msg)	404 Not Found
bad_request(msg)	400 Bad Request
internal_server_error(msg)	500 Internal Server Error

모든 메서드는 Response 객체를 반환하며, 후속적으로 체이닝으로 수정 가능.

⸻

### 3. 헤더 및 상태코드 설정
```cpp
Response::ok("Hello")
  .with_status(202)
  .with_header("X-Custom", "123")
  .with_content_type("application/json");
```
메서드	설명
with_header(k,v)	응답 헤더 추가 또는 덮어쓰기
with_status(code)	직접 상태코드 설정
with_content_type(type)	Content-Type 지정 (기본: text/plain 또는 application/json)


⸻

### 4. 직렬화 처리 (내부용)

이 내용은 internals/response_pipeline.md 참고

  •	.body()는 직렬화된 형태를 반환
	•	Content-Length는 자동 설정됨
	•	헤더 순서는 보장되지 않음

⸻

## 🧪 예제: POST 요청 처리
```cpp
// POST /login
Task<Response> login_handler(const Request& req, Context& ctx) {
    auto dto = req.parse_json_body<LoginDto>();
    if (!dto) {
        return Response::bad_request("Invalid credentials");
    }

    auto token = auth::login(dto->username, dto->password);
    if (!token) {
        return Response::unauthorized("Login failed");
    }

    return Response::ok(R"({"token": ")" + *token + R"("})")
        .with_content_type("application/json");
}
```
