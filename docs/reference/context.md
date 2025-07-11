# Context API Reference

> Official API Reference for `Stellane::Context`  
> For conceptual background, see [concepts/context.md](../concepts/context.md)

---

## 🧭 Overview

`Context`는 Stellane에서 모든 요청(request) 단위로 생성되는 **비동기 안전**, **타입 세이프**, **분산 추적 지원** 데이터 컨테이너입니다.

핸들러와 미들웨어 간의 데이터 공유, 로깅, 트레이싱 등에서 핵심 역할을 수행하며  
각 요청별로 고유한 `trace_id`를 포함하여 전파됩니다.

**이 문서는 Context의 모든 공개 API를 설명합니다.**

---

## 🔓 Class API

```cpp
class Context {
 public:
    const std::string& trace_id() const;

    template <typename T>
    void set(const std::string& key, T value);

    template <typename T>
    std::optional<T> get(const std::string& key) const;

    void log(const std::string& message) const;
};
```

⸻

### 🆔 trace_id()

설명: 현재 요청에 고유하게 부여된 trace_id를 반환합니다.
이 값은 로깅, 트레이싱, 디버깅 시 고유 식별자로 사용됩니다.
```cpp
const std::string& trace_id() const;
```
  •	반환값: 요청 단위 고유 식별자 (예: "0a17f28c-d8b4-11ed-b5ea")
	•	사용 예시:
```cpp
ctx.log("Trace ID = " + ctx.trace_id());
```

⸻

### 🧠 set<T>()

설명: 요청 범위 내의 컨텍스트 저장소에 키-값 쌍을 저장합니다.
T는 임의의 타입으로, 내부적으로 std::any로 감싸져 저장됩니다.
```cpp
template <typename T>
void set(const std::string& key, T value);
```
  •	파라미터:
	•	key: 식별 키 (예: "user_id")
	•	value: 저장할 값 (T는 int, string, struct 등 자유롭게 지정 가능)
	•	사용 예시:
```cpp
ctx.set<int>("user_id", 42);
ctx.set("role", std::string("admin"));
```

⸻

### 🔍 get<T>()

설명: Context에 저장된 값을 타입 안전하게 조회합니다.
요청한 타입 T와 저장된 타입이 다르면 std::nullopt를 반환합니다.
```cpp
template <typename T>
std::optional<T> get(const std::string& key) const;
```
  •	반환값: std::optional<T>
	•	값이 존재하고 타입이 일치하면 T
	•	없거나 타입 불일치 시 std::nullopt
	•	사용 예시:
```cpp
auto id_opt = ctx.get<int>("user_id");
if (id_opt) {
    int uid = *id_opt;
    // uid 사용 가능
} else {
    return Response::unauthorized("user_id not found");
}
```

⸻

### 🪵 log()

설명: 현재 trace_id를 자동 포함한 메시지를 Stellane의 로거에 기록합니다.
```cpp
void log(const std::string& message) const;
```
  •	포맷 예시:
```bash
[2025-07-07T14:03:12Z] [INFO] [trace_id=abcd-1234] User updated profile
```
  •	사용 예시:
```cpp
ctx.log("User successfully authenticated.");
```

⸻

## 💡 종합 예시
```cpp
Task<Response> handler(const Request& req, Context& ctx) {
    auto token = req.header("Authorization");
    auto user_id = auth::verify_token(token);
    if (!user_id) {
        ctx.log("Invalid token");
        return Response::unauthorized();
    }

    ctx.set<int>("user_id", *user_id);

    if (auto uid = ctx.get<int>("user_id"); uid) {
        ctx.log("Processing user " + std::to_string(*uid));
    }

    return Response::ok("Hello");
}
```

⸻

## 🔐 내부 구현 참고

항목	설명
타입 저장소	std::unordered_map<std::string, std::any>
타입 안전성	std::optional<T> + any_cast로 보장
비동기 안전성	Coroutine-local 또는 명시적 전달로 보장됨
생성 위치	요청 수신 시 Server::handle() 내부
기본 저장소	Trace ID, 사용자 인증 정보, 미들웨어 상태 등


⸻

## 🔧 Thread & Coroutine Safety

항목	보장 여부	설명
🧵 Thread Safety	✔	요청마다 독립된 Context 인스턴스 사용
🔄 Coroutine Safety	✔	명시적 Context& 또는 coroutine-local 사용
🛠️ Global Lock-Free	✔	글로벌 동기화 불필요


⸻

## 🔭 확장 예정 기능

항목	상태	설명
ctx.scope(name)	🟡 예정	트레이스 로그에 자동 prefix 추가
Custom Tracer	🟢 일부 구현	OpenTelemetry 등 외부 연동 가능
타입 안정화	🟡 검토 중	variant 혹은 tagged_union 적용 가능성


⸻

## 🔗 관련 문서
	•	concepts/context.md – 개념 및 철학
	•	internals/context_propagation.md – 생성 및 전달 방식
	•	reference/res_req.md – Request, Response API

---
