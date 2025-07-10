 # Middleware in Stellane

> Intercept and enhance every request – modularly, predictably, and asynchronously.

---

## 1. 개요 (Overview)

**미들웨어(Middleware)**는 요청(Request)이 라우터와 핸들러(Handler)에 도달하기 전에,  
또는 응답(Response)이 클라이언트에게 반환되기 전에 **공통 처리 로직을 삽입하는 계층**입니다.

Stellane의 미들웨어는 NestJS와 Express의 철학을 이어받되,  
**C++20 Coroutines 기반의 비동기 컨트롤 흐름**과 **Zero-Cost Abstraction**을 갖춘 형태로 구현되어 있습니다.

핵심 특징:

- 체이닝 가능한 반복 기반 실행 구조 (재귀 호출 아님)
- 미들웨어 순서는 등록한 순서대로 실행됨
- `Context`를 통해 요청 상태 공유
- `next()` 호출 여부에 따라 흐름 제어 가능 (early exit 지원)

---

## 2. 주요 역할 (Responsibilities)

| 역할 | 설명 |
|------|------|
| 🔐 인증 / 권한 검사 | Authorization 헤더 분석 후 유효성 검사 |
| 📊 로깅 / 메트릭 | 요청 시작/종료 시간 기록, 성능 측정 |
| 📦 요청 가공 | Header/Body 검증 및 변환 |
| ❌ 에러 핸들링 | 예외 포착 및 표준 응답 포맷 구성 |
| 🎯 비즈니스 전처리 | 파라미터 유효성 체크, 트랜잭션 초기화 등 |

---

## 3. 기본 구조 및 API (Middleware Interface)

모든 미들웨어는 다음 시그니처를 가진 `handle()` 메서드를 구현하는 객체여야 합니다:

```cpp
class MyMiddleware {
public:
    Task<> handle(const Request& req, Context& ctx, const Next& next) {
        // Pre-processing
        ctx.log("Middleware started.");

        // 다음 미들웨어 또는 핸들러 실행
        co_await next();

        // Post-processing
        ctx.log("Middleware finished.");
    }
};
```
🔸 인자 설명

인자	설명
`Request& req`	현재 요청 객체 (헤더, 파라미터, 바디 등 접근 가능)
`Context& ctx` 요청 범위 컨텍스트 (Trace ID, 사용자 정보 등 공유 가능)
`const Next& next` 다음 미들웨어 또는 핸들러를 실행하는 함수. co_await next(); 필수 호출 아님

> ※ next()를 호출하지 않으면 체인 중단이 발생하며, 이후 핸들러가 호출되지 않습니다. 인증 실패 등의 상황에서 사용됩니다.

⸻

## 4. 사용 예시 (Usage Examples)

### 4.1 LoggingMiddleware
```cpp
class LoggingMiddleware {
public:
    Task<> handle(const Request& req, Context& ctx, const Next& next) {
        ctx.log("Incoming request: " + req.method() + " " + req.path());
        auto start = std::chrono::steady_clock::now();

        co_await next(); // 이후 미들웨어 또는 핸들러 호출

        auto end = std::chrono::steady_clock::now();
        ctx.log("Request processed in " + std::to_string((end - start).count()) + "ns");
    }
};
```

⸻

### 4.2 AuthMiddleware
```cpp
class AuthMiddleware {
public:
    Task<> handle(const Request& req, Context& ctx, const Next& next) {
        auto token = req.header("Authorization");

        if (!token || !verify(token.value())) {
            ctx.log("Unauthorized access attempt");

            // 여기서 던져진 HttpError는 앞서 등록된 ErrorHandlingMiddleware에서 처리됩니다.
            throw HttpError(401, "Unauthorized");
        }

        auto user_id = extract_user_id(token.value());
        ctx.set<int>("user_id", user_id);

        co_await next();
    }
};
```

⸻

### 4.3 ErrorHandlingMiddleware
```cpp
class ErrorHandlingMiddleware {
public:
    Task<> handle(const Request& req, Context& ctx, const Next& next) {
        try {
            co_await next();  // 이후 체인 실행
        } catch (const HttpError& err) {
            ctx.log("Handled HTTP error: " + std::to_string(err.status()));
            throw;  // Stellane이 JSON 에러 응답으로 자동 포맷
        } catch (const std::exception& ex) {
            ctx.log("Unhandled exception: " + std::string(ex.what()));
            throw HttpError(500, "Internal Server Error");
        }
    }
};
```

⸻

## 5. 미들웨어 등록 및 실행 순서

Stellane의 Server 객체는 use() 메서드를 통해 미들웨어를 등록합니다.
등록 순서는 체인 실행 순서에 직결되므로 주의해야 합니다.
```cpp
Server server;

server.use(ErrorHandlingMiddleware{});  // 항상 가장 먼저 등록
server.use(LoggingMiddleware{});
server.use(AuthMiddleware{});

// 라우터 및 핸들러 등록
server.route("/posts", postRouter);
```
🔁 실행 흐름 예시

다음과 같이 미들웨어가 등록된 경우:
```
[Error] → [Logging] → [Auth] → [Handler]
```
실행 순서는 다음과 같습니다:
```
Error (pre)
  └─ Logging (pre)
       └─ Auth (pre)
            └─ Handler 실행
       └─ Auth (post)
  └─ Logging (post)
Error (post)
```
> 📌 이 구조는 러시아 인형(nesting) 혹은 양파 껍질(onion-layer) 구조로 이해하면 쉽습니다.

⸻

## 6. 내부 실행 구조 (Chain Execution Model)

Stellane의 미들웨어 체인은 재귀 없이 반복문 기반으로 설계되어
스택 오버플로 없이 수천 개의 체인도 안정적으로 처리 가능합니다.
```cpp
for (size_t i = 0; i < middleware_chain.size(); ++i) {
    co_await middleware_chain[i].handle(req, ctx, [&] { return next(i + 1); });
}
```
❓ Why no recursion?
	•	코루틴 체계에서 재귀 호출은 디버깅 어려움, 스택 확장, 예외 전파 복잡성 문제 발생
	•	반복 기반 체인은 추적 가능성(traceability) 과 성능 안정성 측면에서 탁월

⸻

## 7. 베스트 프랙티스 (Best Practices)

✅ 권장	🚫 비권장
ctx.set()으로 상태 전달	전역 변수 사용
체인 종료 시 throw 명시	next() 호출 누락
최소한의 책임만 수행	비즈니스 로직 침범
재사용 가능한 미들웨어 분리	모든 라우터에 같은 로직 반복 구현


⸻

## 8. 향후 확장 계획 (Roadmap)
	•	✅ 미들웨어 그룹 기능 (server.useGroup("/api", [...]))
	•	🟡 동적 미들웨어 삽입 (ctx.injectMiddleware(...))
	•	🟡 테스트용 미들웨어 가짜(fakes) 주입 지원 예정
	•	🟡 미들웨어 실행 시간 메트릭 자동 수집 (OpenTelemetry 연동 포함)

⸻
