# Router API Reference

> Official Reference for `Stellane::Router` and routing-related APIs.  
> For conceptual background, see: [concepts/router.md](../concepts/router.md)  
> For internals, see: [internals/routing_tree.md](../internals/routing_tree.md)

---

## 🧭 Overview

`Router`는 HTTP 요청을 처리할 핸들러와 경로를 연결하는 Stellane의 핵심 컴포넌트입니다.  
`Server`는 하나 이상의 `Router`를 지정된 prefix 아래에 mount하여 전체 API 구조를 구성합니다.

이 문서는 다음을 포함합니다:

- `Router`의 메서드별 등록 API
- `Server`의 mount API
- 동적 파라미터 처리 문법
- 실제 구성 예시

---

## 🧱 Router Class API

### 📌 `.get()`, `.post()`, `.put()`, `.del()`, `.patch()`

**설명**: HTTP 메서드와 경로에 대응하는 핸들러를 등록합니다.

```cpp
void get(const std::string& path, HandlerFunction handler);
void post(const std::string& path, HandlerFunction handler);
// ... 기타 메서드 동일
```
  •	path: 요청 경로 문자열 (/, /users, /users/:id 등)
	•	handler: 다음 시그니처를 따르는 비동기 핸들러 함수
```cpp
Task<Response> handler(Context& ctx, /* optional injected parameters */);
```


### 📌 동적 경로(:id, :slug)를 포함한 경우, 해당 파라미터가 핸들러 인자로 자동 주입됩니다.

✅ 예시
```cpp
Router router;

router.get("/", get_home); // GET /
router.post("/login", login_user); // POST /login
router.get("/users/:id", get_user); // GET /users/42
```

⸻

## 🧩 Server Class API

### 🚀 .mount(prefix, router)

설명: 라우터를 지정된 경로 접두사(prefix) 아래에 등록합니다.
```cpp
void mount(const std::string& prefix, const Router& router);
```
  •	모든 경로는 prefix를 기준으로 상대 경로가 붙습니다.
	•	내부적으로 prefix + path 조합이 등록되어 트리에 병합됩니다.

✅ 예시
```cpp
Router user_router;
user_router.get("/:id", get_user); // GET /users/:id

Router post_router;
post_router.get("/:slug", get_post); // GET /posts/:slug

Server server;
server.mount("/users", user_router);
server.mount("/posts", post_router);
```

⸻

## 🧠 Path Matching Syntax

### 📘 정적 경로
  •	정확히 일치하는 경로
	•	예: /about, /users/profile

### 🌀 동적 경로 파라미터
  •	경로 중 일부를 변수로 지정 (:param 형식)
	•	예: /users/:id, /posts/:category/:slug

핸들러 함수에 자동으로 주입됩니다.
```cpp
router.get("/users/:id", get_user);

Task<Response> get_user(Context& ctx, int id) {
    return Response::ok("User ID = " + std::to_string(id));
}
```

⸻

## 🧪 Full Usage Example
```cpp
// ==== handlers ====
Task<Response> login_handler(Context& ctx, const LoginDto& body);
Task<Response> get_user_profile(Context& ctx, int user_id);

// ==== routers ====
Router create_auth_router() {
    Router r;
    r.post("/login", login_handler);
    return r;
}

Router create_user_router() {
    Router r;
    r.get("/:id/profile", get_user_profile);
    return r;
}

// ==== server setup ====
int main() {
    Server server;

    // 전역 미들웨어
    server.use(LoggingMiddleware{});

    // 모듈별 라우터 마운트
    server.mount("/auth", create_auth_router());
    server.mount("/api/users", create_user_router());

    server.listen(8080);
    return 0;
}
```
📌 위 구조는 다음과 같은 경로를 제공합니다:
	•	POST /auth/login
	•	GET /api/users/:id/profile

⸻

## ⚙️ 내부 구현 참고
  •	정적 경로: Trie 기반 검색 → O(1) 수준
	•	동적 경로: 정규식 기반 → 경로 수에 따라 O(n)
	•	정적 경로 우선 매칭 → /users/profile > /users/:id

> → 상세 동작은 internals/routing_tree.md 참조

⸻

## ✅ Best Practices

권장 ✅	지양 🚫
기능별 Router 분리 (auth_router, user_router)	하나의 거대한 router에 모두 등록
mount("/api")로 prefix 관리	모든 경로에 /api/...를 수동 작성
파라미터 타입 자동 변환 활용 (int, string)	모든 파라미터를 문자열로 처리


⸻

## 🔭 향후 확장

기능	상태
파라미터 데코레이터 (@Param)	🟡 검토 중
URI 디코딩 자동 처리	🔜 예정
.group() 기반 Router Nesting	🔜 예정


⸻

🔗 관련 문서
	•	concepts/router.md – 라우팅 개념과 구조
	•	internals/routing_tree.md – 경로 매칭 알고리즘
	•	reference/handler.md – 핸들러 함수 정의와 파라미터 주입 규칙

---
