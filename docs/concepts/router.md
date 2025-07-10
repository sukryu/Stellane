# Routing in Stellane

> Map requests to the right logic with precision and power.

---

## 1. 개요 (Overview)

**라우터(Router)**는 들어온 HTTP 요청을 적절한 **핸들러**로 연결해 주는 역할을 합니다.  
요청의 **URI 경로**와 **HTTP 메서드**를 기반으로 등록된 핸들러를 찾아 실행합니다.

Stellane의 라우터는 다음 기능을 제공합니다:

- ✅ 정적 및 동적 경로 매칭
- 🔄 GET, POST, PUT, DELETE 등 HTTP 메서드별 핸들러 분기
- 🔢 경로 파라미터 추출 및 핸들러 파라미터 자동 주입
- 📂 라우터 단위의 모듈화 및 그룹 마운트

---

## 2. 주요 역할 (Responsibilities)

| 역할 | 설명 |
|------|------|
| 🛣️ 경로 매칭 | `/users` 같은 정적 경로, `/users/:id` 같은 동적 경로 처리 |
| 🔁 메서드 기반 분기 | 같은 경로라도 GET, POST 등 메서드에 따라 다른 핸들러 연결 |
| 🔍 파라미터 추출 | 경로 내 변수(ex: `:id`)를 추출하여 핸들러 함수 인자로 전달 |
| 📂 그룹화 및 마운트 | 관련 경로를 하나의 `Router`로 구성하여 `/api`, `/auth` 등 prefix 아래에 등록 |

---

## 3. 기본 구조 및 API

```cpp
#include <stellane/router.h>

// 라우터 객체 생성
Router user_router;

// 경로 및 메서드에 핸들러 등록
user_router.get("/", get_all_users);
user_router.post("/", create_user);
user_router.get("/:id", get_user_by_id);
user_router.put("/:id", update_user);
user_router.del("/:id", delete_user);

// 메인 서버에 마운트
Server server;
server.mount("/users", user_router);
```
✅ router.get(path, handler)은 해당 경로로 들어오는 GET 요청을 지정된 핸들러에 연결합니다.
✅ server.mount(prefix, router)는 모든 경로에 prefix를 자동으로 붙여 하나의 모듈로 묶습니다.

⸻

## 4. 사용 예시 (Usage Examples)

### 4.1 기본 라우팅
```cpp
Router router;

router.get("/", home_handler);
router.get("/about", about_handler);
```

⸻

### 4.2 동적 경로 파라미터
```cpp
// GET /articles/tech/123
router.get("/articles/:category/:article_id", get_article_handler);

// 핸들러 시그니처
Task<Response> get_article_handler(Context& ctx, std::string category, int article_id);
```
> 📌 category = “tech”, article_id = 123 자동 주입됨

⸻

### 4.3 모듈화된 라우터 마운트
```cpp
auth_routes.cpp

Router auth_router;

auth_router.post("/login", login_handler);
auth_router.post("/register", register_handler);

server.cpp

Server server;

server.mount("/auth", auth_router);
// 결과: /auth/login, /auth/register 경로 자동 생성
```

⸻

## 5. 경로 매칭 규칙 (Path Matching Rules)

규칙	설명
📌 정적 경로 우선	/users/profile은 /users/:id보다 우선 매칭
🔢 파라미터 문법	:param 형식으로 선언 (예: /users/:id)
🧠 내부 구현	내부적으로 정적 경로는 Trie, 동적 경로는 정규식 기반 매칭 사용

예시: /users/:id/settings → { "id": 42 } 로 자동 파싱되어 핸들러에 주입됩니다.

⸻

## 6. 베스트 프랙티스 (Best Practices)

✅ 권장	🚫 비권장
기능별로 라우터 분리 (user_router, auth_router)	모든 경로를 하나의 거대한 파일에 정의
RESTful URI + 메서드 조합	GET으로 리소스를 생성하는 등 HTTP 의미 위반
공통 prefix는 mount로 관리	매 경로에 /api/를 반복해서 작성
동적 파라미터는 명확한 타입으로 받기	모든 파라미터를 string으로 받기


⸻

## 7. 향후 확장 계획 (Planned Enhancements)
	•	✅ Path parameter 타입 자동 변환 (이미 지원)
	•	✅ 트리 기반 라우터 성능 개선 (정적 경로: O(1), 동적 경로: O(log n))
	•	🔜 URI 디코딩 및 normalization 옵션 추가
	•	🔜 라우터 데코레이터 문법 (@Get("/users/:id"))

⸻
