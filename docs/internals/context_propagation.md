 # Context Propagation in Stellane

> "요청의 생명 주기를 따라 안전하게 흐르는 상태(State)의 흐름"

---

## 1. 개요 (Overview)

Stellane은 미들웨어 → 핸들러로 이어지는 비동기 체인 구조를 갖고 있으며,  
요청(Request) 단위의 상태를 공유하기 위해 `Context` 객체를 사용합니다.

이 문서에서는 **이 Context 객체가 비동기 흐름 내에서 어떻게 전파(propagation)**되고,  
**중간에서 끊기지 않고 일관성을 유지하는지** 그 구조와 원리를 설명합니다.

---

## 2. Context의 요구 조건 (Requirements)

Stellane에서 Context는 단순한 key-value 저장소를 넘어 다음과 같은 특성을 가져야 합니다:

| 요구 사항 | 설명 |
|-----------|------|
| ✅ 요청 단위 범위(Scope) | 하나의 HTTP 요청에서만 유효해야 함 (thread-safe) |
| ✅ 비동기 전파 가능 | `co_await` 이후에도 context가 유지되어야 함 |
| ✅ 중간 수정 가능 | 미들웨어나 핸들러에서 데이터를 추가하거나 갱신 가능 |
| ✅ 가벼움 & 고성능 | per-request 구조이므로 최소한의 오버헤드로 동작해야 함 |

---

## 3. 실행 모델: 체인 기반 실행 흐름 (Middleware Chain Execution Model)

Stellane은 **재귀 없는 반복문 기반의 체인 실행 모델**을 사용합니다.  
이로 인해 미들웨어 간의 흐름 제어가 명확하고, Context의 수명 주기 관리도 간단해집니다.

```cpp
Task<> execute_chain(Request req, std::shared_ptr<Context> ctx) {
    for (auto& middleware : middleware_list) {
        co_await middleware.handle(req, *ctx, next);
    }
}
```
모든 미들웨어와 핸들러는 **동일한 Context 객체의 참조(Reference)**를 공유하며 실행됩니다.

⸻

## 4. 비동기 전파의 구조 (Async Propagation Mechanism)

Stellane은 C++20/23 Coroutine 기반으로 비동기 흐름을 구성하며,
Context는 아래와 같은 방식으로 전파됩니다:
	•	Context는 shared_ptr<Context>로 생성됨 (copy-on-write 방지)
	•	미들웨어/핸들러는 모두 Context&를 참조로 받음
	•	co_await 이후에도 같은 객체를 참조하므로 상태가 끊기지 않음

💡 예시
```cpp
Task<> AuthMiddleware::handle(const Request& req, Context& ctx, const Next& next) {
    ctx.set("user_id", 42);         // 인증 정보 저장
    co_await next();                // 다음으로 전달
    auto trace = ctx.get<std::string>("trace_id");
}
```
→ co_await 이전/이후에도 ctx는 동일한 인스턴스를 참조합니다.

⸻

## 5. 설계 구조도 (Architecture Diagram)
```mermaid
graph TD
    subgraph 요청 흐름
        A[Request] --> B[Middleware A]
        B --> C[Middleware B]
        C --> D[Handler]
        D --> E[Response]
    end
    subgraph Context 공유
        B -.->|ctx.set(...)| ctx[(Context)]
        C -.->|ctx.get(...)| ctx
        D -.->|ctx.get(...)| ctx
    end
```
  •	하나의 Context 객체가 전체 체인에서 공유됨을 시각적으로 표현
	•	미들웨어와 핸들러 모두 읽기/쓰기 가능

⸻

## 6. Context 내부 구조 (Internal Representation)
```cpp
class Context {
private:
    std::unordered_map<std::string, std::any> data;
    std::unordered_map<std::string, std::shared_ptr<void>> extensions;
    Logger logger;
public:
    template<typename T>
    void set(std::string key, T value);

    template<typename T>
    std::optional<T> get(std::string key) const;

    Logger& log();
};
```
특징:
	•	std::any 기반의 타입 안전한 저장소
	•	커스텀 확장을 위한 extensions 슬롯 존재
	•	로깅, 트레이싱 등을 위한 하위 모듈 포함 가능

⸻

## 7. 요청 범위 수명 관리 (Lifetime Management)

Context는 다음 조건을 만족합니다:
	•	서버가 요청을 수신 → Context가 생성됨
	•	체인 전체에서 **동일한 shared_ptr**가 전파됨
	•	응답이 완료되면 Context도 자동 소멸됨 (스마트 포인터 참조 해제)
```cpp
void handle_request(const Request& req) {
    auto ctx = std::make_shared<Context>();
    co_await execute_chain(req, ctx); // 체인 실행
    // 여기서 ctx는 더 이상 사용되지 않음 → 자동 소멸
}
```

⸻

## 8. 성능 및 안전성 고려 (Performance & Safety)

고려 항목	설명
메모리 오버헤드	요청당 단일 객체, 경량 구조 설계
스레드 안전성	동시 요청은 별도 Context 인스턴스를 사용
동시성 이슈 없음	Context는 요청 범위 전용, 공유 없음
복사 최소화	대부분 참조 기반 전달 (값 복사 없음)


⸻

## 9. FAQ 및 설계 배경

### Q. 왜 TLS(Thread Local Storage)를 사용하지 않았나요? 
  •	TLS는 C++ 코루틴과 궁합이 맞지 않으며, 실행 컨텍스트가 스케줄링에 따라 달라질 수 있어 위험합니다.
	•	Stellane은 명시적인 Context 전달 방식(shared_ptr)을 사용함으로써 예측 가능성, 디버깅 용이성, 안정성을 확보했습니다.

### Q. 중간에서 Context를 수정해도 괜찮나요?
  •	네. 모든 컴포넌트는 동일한 객체를 참조하므로, ctx.set("foo", bar)는 이후 체인에서 그대로 반영됩니다.

⸻
