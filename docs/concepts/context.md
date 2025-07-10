# Context in Stellane

> A structured, traceable, and coroutine-safe request-scoped data store.

---

## 1. 개요 (Overview)

`Context`는 **Stellane 프레임워크의 핵심 컴포넌트**로,  
각 요청(request)마다 생성되는 **스레드 안전**하고 **비동기 전파 가능한 데이터 컨테이너**입니다.

모든 미들웨어, 핸들러, 내부 트레이싱 시스템은 이 `Context` 객체를 통해 정보를 주고받습니다.  
이는 **Trace ID 기반 로깅**, **사용자 정보 보존**, **비즈니스 상태 전달** 등  
**요청 단위의 상태 관리**와 **비동기 흐름의 디버깅**을 위해 필수적인 구조입니다.

---

## 2. 주요 역할 (Responsibilities)

| 역할 | 설명 |
|------|------|
| 🔖 Trace ID 관리 | 모든 요청에 고유한 `trace_id`를 생성 및 전파 |
| 🧠 데이터 저장 | `std::string` → `std::any` 구조의 타입 세이프 저장소 |
| 👤 사용자 인증 정보 | user_id, role 등 보안 관련 메타데이터 저장 |
| 🔄 상태 전달 | 요청 간 로직 상태 공유 (e.g. `ctx.set("validated", true)`) |
| 🔍 디버깅용 로그 연동 | 로거 시스템과 통합되어 자동 trace_id 부착 |

---

## 3. 기본 구조 (Structure)

```cpp
class Context {
 public:
    // Constructor (internal use only)
    explicit Context(std::string trace_id);

    // Get current trace ID
    const std::string& trace_id() const;

    // Set key-value data
    template <typename T>
    void set(const std::string& key, T value);

    // Get value by key (returns optional)
    template <typename T>
    std::optional<T> get(const std::string& key) const;

    // Logging helper
    void log(const std::string& message) const;

 private:
    std::string trace_id_;
    std::unordered_map<std::string, std::any> store_;
};
```
특이 사항:
	•	std::any를 사용해 타입 자유도를 확보하되, template get/set<T>()으로 안전성 유지
	•	Context는 일반적으로 Context& 레퍼런스로 전달되어 수정이 가능하며, 상태 조회가 필요할 때만 const Context&로 전달될 수 있습니다.
	•	내부적으로는 coroutine-local storage 또는 전달 방식으로 보존됨

⸻

## 4. 생성 및 전파 방식

### 4.1 생성 위치
	•	모든 요청은 Server에서 수신되는 순간 Context 객체를 생성합니다.
	•	Trace ID는 UUID 또는 고해상도 timestamp + 난수 조합으로 생성됩니다.
```cpp
// Server.cc
Context ctx = Context::create_with_random_trace_id();
```
### 4.2 전파 방식

Stellane은 C++ coroutine의 흐름에 따라 Context를 명시적으로 또는 암묵적으로 전파합니다.
	•	명시적 전파(Context& 인자):
    • 장점: 의존성이 명확하고 테스트가 용이함.
    • 단점: 보일러플레이트 코드가 늘어남.
	•	암묵적 전파 (선택적): 내부적으로 coroutine-local storage를 통해 전달 가능 (비활성화 시 명시적 전달 사용)
    • 장점: 코드가 간결해짐.
    • 단점: 동작이 다소 마법처럼 느껴질 수 있고, 특정 플랫폼에서 미세한 성능 오버헤드가 발생할 수 있음.
```cpp
Task<Response> handler(const Request& req, Context& ctx) {
    auto user = ctx.get<User>("user");
    ctx.log("Fetching user profile");
}
```

⸻

## 5. 주요 메서드 설명

### 5.1 `trace_id()`

현재 요청의 trace ID를 반환합니다. 모든 로그와 로깅 시스템에 자동 첨부됩니다.

### 5.2 `set<T>(key, value)`

요청 범위 내에 특정 데이터를 저장합니다.
```cpp
ctx.set<int>("user_id", 42);
ctx.set<std::string>("role", "admin");
```
### 5.3 `get<T>(key)`

저장된 데이터를 타입 안전하게 꺼냅니다. 값이 없으면 std::nullopt.
```cpp
auto uid = ctx.get<int>("user_id");
if (uid.has_value()) { ... }
```
### 5.4 `log(message)`

현재 context에 연결된 logger를 통해 trace ID와 함께 로그를 출력합니다.
```cpp
ctx.log("User successfully authenticated.");
// Output: [trace_id=abc-123] User successfully authenticated.
```

⸻

## 6. 사용 예시

### 6.1 사용자 인증 정보 전달
```cpp
Task<Response> auth_middleware(Request& req, Context& ctx) {
    auto token = req.header("Authorization");
    auto user_id = verify_token(token);
    if (!user_id) return Response::unauthorized();

    ctx.set<int>("user_id", user_id.value());
    co_return {};
}
```
### 6.2 오류 처리 및 트레이싱
```cpp
Task<Response> get_user(Request& req, Context& ctx) {
    auto uid = ctx.get<int>("user_id");
    if (!uid) {
        ctx.log("User ID missing in context");
        return Response::internal_server_error();
    }

    ctx.log("Fetching profile for user " + std::to_string(uid.value()));
    ...
}
```

⸻

## 7. Thread Safety & 비동기 안전성

항목	보장 여부
🧵 스레드 안전성	✔ (단일 요청 내에서만 공유됨)
🔁 coroutine 안전성	✔ (Context&가 명시적 전달됨, 또는 TLS 기반)
🔒 글로벌 동기화 필요 없음	✔


⸻

## 8. 관련 기술 자료
	•	architecture.md > Context Section
	•	std::any Reference (cppreference)
	•	Coroutines and TLS in C++

⸻

## 9. 향후 확장 계획

항목	설명
✅ 사용자 정의 트레이서 연결	외부 로깅 시스템으로 bridge (OpenTelemetry 등)
🟡 타입 안정성 강화	variant, tagged union 기반 구현 옵션 고려
🟡 ctx.scope(name)	자동 prefix를 포함하는 스코프 기반 트레이싱 기능 추가 예정


