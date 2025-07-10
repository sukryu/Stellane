# Handlers in Stellane

> The final destination of a request – where logic meets execution.

---

## 1. 개요 (Overview)

**핸들러(Handler)**는 클라이언트의 요청을 최종적으로 처리하는 **비즈니스 로직의 중심**입니다.  
라우터에 의해 특정 경로와 HTTP 메서드에 매핑되며,  
미들웨어 체인을 모두 통과한 요청을 받아 적절한 **응답(Response)**을 생성합니다.

Stellane의 핸들러는 다음과 같은 특징을 지닙니다:

- ✅ **타입 세이프한 파라미터 주입**  
- 🔄 **비동기 응답 처리 (`Task<Response>`)**  
- 🧩 **Context 기반 정보 공유 및 추적**  
- 🧱 **서비스 계층과의 명확한 역할 분리**

---

## 2. 주요 역할 (Responsibilities)

| 역할 | 설명 |
|------|------|
| 📥 요청 데이터 처리 | 경로 파라미터, 쿼리 스트링, 본문 등을 자동 추출하여 타입에 맞게 주입 |
| 🧠 비즈니스 로직 실행 | DB 조회, 트랜잭션 처리, 외부 API 호출 등 핵심 도메인 로직 수행 |
| 🧩 Context 활용 | 인증, 트레이싱, 상태 공유 등 미들웨어가 저장한 정보를 사용 |
| 📤 응답 생성 | 처리 결과에 따라 다양한 HTTP 응답 생성 및 반환 |

---

## 3. 기본 구조 및 API

### 🔸 기본 시그니처

```cpp
Task<Response> handler(const Request& req, Context& ctx);
```
### 🔸 파라미터 주입 확장

Stellane은 타입 기반의 메타프로그래밍을 활용해 핸들러 함수에 자동으로 인자를 주입합니다:
```cpp
// GET /users/:id
Task<Response> get_user_by_id(Context& ctx, int id);

// POST /users
Task<Response> create_user(Context& ctx, const CreateUserDto& body);
```
> 📌 자동 파라미터 주입 원리
Stellane은 내부적으로 요청의 JSON 본문을 파싱하여 DTO 타입으로 변환합니다.
변환에 실패할 경우, 자동으로 400 Bad Request 응답을 반환하며 핸들러는 호출되지 않습니다.

⸻

## 4. 사용 예시 (Usage Examples)

### 4.1 경로 파라미터 사용
```cpp
// GET /posts/:post_id
Task<Response> get_post(Context& ctx, int post_id) {
    auto post = db::find_post_by_id(post_id);

    if (!post) {
        return Response::not_found("Post not found");
    }

    return Response::ok(post->to_json());
}
```

⸻

### 4.2 Context + 본문(DTO) 사용
```cpp
// POST /posts
Task<Response> create_post(Context& ctx, const CreatePostDto& new_post) {
    // AuthMiddleware에서 설정한 user_id를 꺼냄
    auto user_id = ctx.get<int>("user_id").value();

    auto created = db::create_post(user_id, new_post);

    return Response::created(created.to_json());
}
```
> 🔁 이 예시는 Stellane의 핵심 아키텍처(Context → Middleware → Handler)가
실제로 어떻게 유기적으로 연결되는지를 잘 보여줍니다.

⸻

## 5. 핸들러 등록

작성한 핸들러는 Router를 통해 메서드 및 경로에 등록합니다:
```cpp
Router post_router;

// 핸들러 등록
post_router.get("/:post_id", get_post);     // GET /posts/:post_id
post_router.post("/", create_post);         // POST /posts

// 메인 서버에 마운트
Server server;
server.mount("/posts", post_router);
```
> 📌 라우터는 URI 네임스페이스를 기준으로 기능 모듈을 분리할 수 있습니다.

⸻

## 6. 응답 생성하기 (Response Creation)

Stellane은 직관적인 응답 생성 API를 제공합니다:
```cpp
// 200 OK with JSON
Response::ok(R"({"message": "Success"})");

// 201 Created
Response::created(new_user.to_json());

// 404 Not Found
Response::not_found("User not found");

// 500 Error with custom headers
Response::internal_server_error("DB failure")
    .with_header("Retry-After", "30");
```

⸻

## 7. 베스트 프랙티스 (Best Practices)

✅ 권장	🚫 비권장
핸들러는 가볍고 명확하게 유지	비즈니스 로직을 핸들러에 몰아넣음
서비스 계층으로 도메인 로직 분리	핸들러에서 직접 DB 연결을 관리
Context를 통해 인증 및 로깅 연계	핸들러 내부에서 인증 헤더 파싱
DTO 기반 타입-세이프 본문 처리	req.body()로 직접 JSON 파싱 후 수동 처리


⸻

## 8. 향후 확장 계획 (Roadmap)
	•	✅ 데코레이터 기반 선언 (예: @Get("/posts/:id"))
	•	✅ OpenAPI 문서 자동 생성
	•	✅ DTO 자동 검증 (JSON Schema 기반)
	•	✅ 핸들러 단위 트레이싱 시각화 (TraceID 연계)

⸻
