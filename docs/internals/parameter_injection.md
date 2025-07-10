# Parameter Injection in Stellane

> 타입 안전성과 DX를 동시에: Stellane의 자동 파라미터 주입 시스템

---

## 1. 개요 (Overview)

Stellane은 핸들러 함수에 다음과 같은 형태로 자연스럽게 파라미터를 전달할 수 있도록 설계되었습니다:

```cpp
Task<Response> get_user(Context& ctx, int user_id);
Task<Response> create_post(Context& ctx, const CreatePostDto& body);
```
개발자는 별도의 파싱 로직 없이도 int, std::string, 구조체 등의 파라미터를 함수 인자로 직접 받을 수 있습니다.
이러한 기능은 “파라미터 자동 주입(Parameter Injection)” 시스템 덕분에 가능하며, 이 문서에서는 그 내부 작동 방식을 상세히 설명합니다.

⸻

## 2. 설계 목표 (Design Goals)

목표	설명
✅ 타입 안전성	컴파일 타임에 가능한 모든 오류를 사전에 방지
✅ DX 개선	파라미터 추출, 바인딩, 유효성 검사를 최소한의 코드로 가능하게
✅ 고성능	런타임 비용을 최소화하여 핸들러 호출에 병목이 없도록
✅ 확장성	새로운 타입을 쉽게 추가 가능하도록 확장 지향적인 설계


⸻

## 3. 주입 가능한 파라미터 타입 (Injectable Types)

Stellane은 핸들러 시그니처를 분석하여 다음 타입들을 자동 주입합니다:

파라미터 타입	주입 대상
Context&	현재 요청의 컨텍스트
Request&	원본 요청 객체
int, std::string, uuid, …	경로 파라미터 또는 쿼리 스트링
사용자 정의 구조체(DTO)	JSON Body → C++ 구조체로 디코딩


⸻

## 4. 주입 과정 단계별 설명 (Injection Pipeline)

### Step 1. 핸들러 시그니처 분석

Stellane은 std::function_traits 기반의 메타프로그래밍을 통해 핸들러의 인자 타입들을 컴파일 타임에 추출합니다.

예:
```cpp
Task<Response> get_user(Context&, int id);
```
→ 분석 결과:
	1.	Context&: 컨텍스트 제공
	2.	int: 경로 파라미터로 추출 필요

⸻

### Step 2. 주입 매커니즘 분기

인자의 타입에 따라 다음과 같이 분기됩니다:

타입 분류	처리 방법
Context&, Request&	내부 인프라 객체 → 그대로 주입
POD 타입(int, string 등)	경로 or 쿼리 파라미터에서 추출 후 변환
사용자 정의 타입	JSON 본문 파싱 후 → 구조체로 역직렬화


⸻

### Step 3. 경로 파라미터 매핑

/users/:id와 같은 경로에서 추출된 값은 문자열(string) 형태입니다. 이를 아래와 같이 변환합니다:
```cpp
std::string raw = "42";
int id = std::stoi(raw);
```
  •	파싱 실패 시 자동으로 400 Bad Request 반환
	•	변환기는 from_string<T>(const std::string&) 형태의 커스터마이징 지원 예정

⸻

### Step 4. DTO 바인딩 (JSON Body)

핸들러에 사용자 정의 구조체가 있을 경우:
```cpp
Task<Response> create_user(Context& ctx, const CreateUserDto& body);
```
Stellane은 다음 과정을 자동으로 수행합니다:
	1.	Content-Type이 application/json인지 확인
	2.	Body를 읽어 nlohmann::json 또는 유사 라이브러리로 파싱
	3.	DTO 구조체로 역직렬화 → 실패 시 400 에러 반환

요구사항:
	•	DTO는 from_json() 또는 nlohmann::json 직렬화 지원 필수

⸻

## 5. 내부 구조 (Core Components)

컴포넌트	설명
HandlerInvoker	실제 핸들러를 호출하기 위해 인자를 조합하는 템플릿 클래스
ParamExtractor<T>	타입별 파라미터 추출기, 특수화로 다양한 타입 지원
BinderContext	추출된 인자와 내부 데이터를 연결하는 임시 저장소
InjectionError	주입 실패 시 발생하는 예외 또는 에러 타입

핵심 구조 예시:
```cpp
template<typename T>
struct ParamExtractor;

template<>
struct ParamExtractor<int> {
    static std::optional<int> extract(const Request& req, const RouteParams& params);
};

template<typename... Args>
class HandlerInvoker {
public:
    Task<Response> invoke(Request req, Context ctx);
};
```

⸻

## 6. 커스터마이징 (Customization)

개발자는 자신만의 타입에 대해 파라미터 추출기를 등록할 수 있습니다:
```cpp
struct UserAgent {
    std::string value;
};

// 특수화 정의
template<>
struct ParamExtractor<UserAgent> {
    static std::optional<UserAgent> extract(const Request& req, const RouteParams&) {
        if (auto ua = req.header("User-Agent"); ua.has_value()) {
            return UserAgent{*ua};
        }
        return std::nullopt;
    }
};
```
이후 핸들러에서 사용 가능:
```cpp
Task<Response> handler(Context& ctx, UserAgent ua);
```

⸻

## 7. 성능 고려사항 (Performance Considerations) 
  •	핸들러 시그니처 해석은 컴파일 타임 또는 서버 부트 시 1회 수행
	•	모든 파라미터 추출은 Lazy Evaluation → 필요한 경우에만 실행
	•	DTO 역직렬화는 Body를 1회만 읽고 캐시된 JSON 객체로 처리

⸻

## 8. FAQ 및 설계 배경 (Design Rationale)

### Q. 모든 파라미터를 Context에서 꺼내는 방식은 어땠나요?
  •	Context 방식은 유연하지만 타입 안전성과 DX 측면에서 한계가 있습니다.
	•	핸들러 시그니처에 타입 명시를 통해 IDE 자동 완성, 컴파일 타임 보장, 문서화 효과까지 얻을 수 있습니다.

### Q. 왜 std::any, void* 기반이 아닌가요?
  •	런타임 타입 검사 방식은 비용이 크고, 유지보수가 어렵습니다.
	•	Stellane은 메타프로그래밍 기반 정적 바인딩 방식을 채택해 성능과 안정성을 모두 확보했습니다.

⸻
