#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <regex>
#include <optional>

#include “stellane/http/request.h”
#include “stellane/http/response.h”
#include “stellane/core/context.h”

namespace stellane {

// Forward declarations
template<typename T = void>
class Task;

// ============================================================================
// 핸들러 타입 정의
// ============================================================================

/**

- @brief 기본 핸들러 함수 타입
- 
- 모든 핸들러는 Context와 Request를 받아 Response를 반환하는 비동기 함수입니다.
  */
  using HandlerFunction = std::function<Task<Response>(Context&, const Request&)>;

/**

- @brief 라우팅 매치 결과
  */
  struct RouteMatch {
  HandlerFunction handler;                                    ///< 매칭된 핸들러
  std::unordered_map<std::string, std::string> path_params;  ///< 추출된 경로 파라미터
  std::string matched_pattern;                               ///< 매칭된 패턴 (디버그용)
  
  RouteMatch(HandlerFunction h, std::unordered_map<std::string, std::string> params = {},
  std::string pattern = “”)
  : handler(std::move(h)), path_params(std::move(params)), matched_pattern(std::move(pattern)) {}
  };

// ============================================================================
// 라우팅 트리 노드 (내부 구현)
// ============================================================================

namespace detail {

/**

- @brief 정적 경로용 Trie 노드
- 
- /users/profile, /users/settings 같은 정적 경로를 O(1) 수준으로 빠르게 매칭
  */
  struct TrieNode {
  std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
  std::unordered_map<std::string, HandlerFunction> handlers;  // HTTP 메서드별 핸들러
  
  TrieNode() = default;
  TrieNode(const TrieNode&) = delete;
  TrieNode& operator=(const TrieNode&) = delete;
  TrieNode(TrieNode&&) = default;
  TrieNode& operator=(TrieNode&&) = default;
  };

/**

- @brief 동적 경로용 패턴 매칭
- 
- /users/:id, /posts/:category/:slug 같은 동적 경로를 정규식으로 처리
  */
  struct DynamicRoute {
  std::string pattern;                                        ///< 원본 패턴 (/users/:id)
  std::regex compiled_regex;                                  ///< 컴파일된 정규식
  std::vector<std::string> param_names;                      ///< 파라미터 이름들 ([“id”])
  std::unordered_map<std::string, HandlerFunction> handlers; ///< HTTP 메서드별 핸들러
  
  DynamicRoute(std::string pat, std::regex regex, std::vector<std::string> params)
  : pattern(std::move(pat)), compiled_regex(std::move(regex)), param_names(std::move(params)) {}
  };

} // namespace detail

// ============================================================================
// 메인 Router 클래스
// ============================================================================

/**

- @brief Stellane의 하이브리드 라우팅 시스템
- 
- 정적 경로는 Trie로 O(1) 매칭, 동적 경로는 정규식으로 유연한 매칭을 제공합니다.
- 
- 특징:
- - 정적 경로 우선 매칭 (/users/profile > /users/:id)
- - 타입 안전한 파라미터 주입
- - HTTP 메서드별 핸들러 분리
- - 라우터 중첩 및 마운트 지원
- 
- @example
- ```cpp
  
  ```
- Router user_router;
- user_router.get(”/:id”, get_user_handler);
- user_router.post(”/”, create_user_handler);
- user_router.put(”/:id”, update_user_handler);
- 
- Router api_router;
- api_router.mount(”/users”, user_router);
- ```
  
  ```

*/
class Router {
public:
/**
* @brief 기본 생성자
*/
Router();

```
/**
 * @brief 복사 생성자 (삭제됨)
 */
Router(const Router&) = delete;
Router& operator=(const Router&) = delete;

/**
 * @brief 이동 생성자
 */
Router(Router&&) = default;
Router& operator=(Router&&) = default;

/**
 * @brief 소멸자
 */
~Router() = default;

// ========================================================================
// HTTP 메서드별 라우트 등록
// ========================================================================

/**
 * @brief GET 요청 핸들러 등록
 * @param path 경로 패턴 (예: "/users/:id")
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& get(const std::string& path, HandlerFunction handler);

/**
 * @brief POST 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& post(const std::string& path, HandlerFunction handler);

/**
 * @brief PUT 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& put(const std::string& path, HandlerFunction handler);

/**
 * @brief DELETE 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& del(const std::string& path, HandlerFunction handler);

/**
 * @brief PATCH 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& patch(const std::string& path, HandlerFunction handler);

/**
 * @brief OPTIONS 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& options(const std::string& path, HandlerFunction handler);

/**
 * @brief HEAD 요청 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& head(const std::string& path, HandlerFunction handler);

/**
 * @brief 모든 HTTP 메서드에 대한 핸들러 등록
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& all(const std::string& path, HandlerFunction handler);

/**
 * @brief 특정 HTTP 메서드에 대한 핸들러 등록
 * @param method HTTP 메서드 (대소문자 무관)
 * @param path 경로 패턴
 * @param handler 핸들러 함수
 * @return Router& (체이닝)
 */
Router& route(const std::string& method, const std::string& path, HandlerFunction handler);

// ========================================================================
// 라우터 중첩 및 마운트
// ========================================================================

/**
 * @brief 다른 라우터를 특정 경로 하위에 마운트
 * @param prefix 마운트할 경로 접두사 (예: "/api/v1")
 * @param sub_router 하위 라우터
 * @return Router& (체이닝)
 * 
 * @example
 * ```cpp
 * Router user_router;
 * user_router.get("/:id", get_user);  // /api/users/:id
 * 
 * Router api_router;
 * api_router.mount("/users", user_router);
 * ```
 */
Router& mount(const std::string& prefix, const Router& sub_router);

// ========================================================================
// 라우트 매칭
// ========================================================================

/**
 * @brief 요청에 매칭되는 핸들러 찾기
 * @param method HTTP 메서드
 * @param path 요청 경로
 * @return 매칭 결과 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<RouteMatch> match(const std::string& method, const std::string& path) const;

/**
 * @brief Request 객체로 매칭되는 핸들러 찾기
 * @param request HTTP 요청
 * @return 매칭 결과 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<RouteMatch> match(const Request& request) const;

// ========================================================================
// 라우트 정보 조회
// ========================================================================

/**
 * @brief 등록된 모든 라우트 패턴 반환
 * @return 라우트 패턴 목록
 */
[[nodiscard]] std::vector<std::string> list_routes() const;

/**
 * @brief 특정 경로가 등록되어 있는지 확인
 * @param method HTTP 메서드
 * @param path 경로 패턴
 * @return 등록 여부
 */
[[nodiscard]] bool has_route(const std::string& method, const std::string& path) const;

/**
 * @brief 특정 경로에서 지원하는 HTTP 메서드 목록 반환
 * @param path 경로 패턴
 * @return 지원하는 메서드 목록
 */
[[nodiscard]] std::vector<std::string> allowed_methods(const std::string& path) const;

/**
 * @brief 등록된 라우트 개수 반환
 * @return 라우트 개수
 */
[[nodiscard]] size_t route_count() const;

// ========================================================================
// 미들웨어 지원 (향후 확장)
// ========================================================================

/**
 * @brief 라우터 전용 미들웨어 등록 (향후 구현)
 * @param middleware 미들웨어 함수
 * @return Router& (체이닝)
 */
// Router& use(MiddlewareFunction middleware);

// ========================================================================
// 디버그 및 유틸리티
// ========================================================================

/**
 * @brief 라우팅 테이블을 문자열로 출력 (디버그용)
 * @param include_handlers 핸들러 정보 포함 여부
 * @return 포맷된 라우팅 테이블
 */
[[nodiscard]] std::string to_string(bool include_handlers = false) const;

/**
 * @brief 라우팅 통계 정보 반환
 * @return 통계 정보 문자열
 */
[[nodiscard]] std::string statistics() const;
```

private:
// ========================================================================
// 내부 데이터 구조
// ========================================================================

```
/// 정적 경로용 Trie 루트 (메서드별)
std::unordered_map<std::string, std::unique_ptr<detail::TrieNode>> static_routes_;

/// 동적 경로 목록 (메서드별)
std::unordered_map<std::string, std::vector<detail::DynamicRoute>> dynamic_routes_;

/// 마운트된 하위 라우터들
std::vector<std::pair<std::string, std::shared_ptr<Router>>> mounted_routers_;

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

/**
 * @brief 경로가 정적인지 동적인지 판단
 * @param path 경로 패턴
 * @return 정적이면 true, 동적이면 false
 */
[[nodiscard]] static bool is_static_path(const std::string& path);

/**
 * @brief 동적 경로 패턴을 정규식으로 컴파일
 * @param path 경로 패턴 (예: "/users/:id/posts/:post_id")
 * @return 컴파일된 정규식과 파라미터 이름들
 */
[[nodiscard]] static std::pair<std::regex, std::vector<std::string>> 
    compile_dynamic_pattern(const std::string& path);

/**
 * @brief 경로를 세그먼트로 분할
 * @param path 경로 문자열
 * @return 세그먼트 목록
 */
[[nodiscard]] static std::vector<std::string> split_path(const std::string& path);

/**
 * @brief 경로 정규화 (앞뒤 슬래시 처리)
 * @param path 원본 경로
 * @return 정규화된 경로
 */
[[nodiscard]] static std::string normalize_path(const std::string& path);

/**
 * @brief HTTP 메서드 정규화 (대문자 변환)
 * @param method 원본 메서드
 * @return 정규화된 메서드
 */
[[nodiscard]] static std::string normalize_method(const std::string& method);

/**
 * @brief 정적 라우트를 Trie에 추가
 * @param method HTTP 메서드
 * @param path 경로
 * @param handler 핸들러
 */
void add_static_route(const std::string& method, const std::string& path, HandlerFunction handler);

/**
 * @brief 동적 라우트 추가
 * @param method HTTP 메서드
 * @param path 경로 패턴
 * @param handler 핸들러
 */
void add_dynamic_route(const std::string& method, const std::string& path, HandlerFunction handler);

/**
 * @brief 정적 라우트에서 매칭 시도
 * @param method HTTP 메서드
 * @param path 요청 경로
 * @return 매칭 결과 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<RouteMatch> match_static_route(const std::string& method, const std::string& path) const;

/**
 * @brief 동적 라우트에서 매칭 시도
 * @param method HTTP 메서드
 * @param path 요청 경로
 * @return 매칭 결과 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<RouteMatch> match_dynamic_route(const std::string& method, const std::string& path) const;

/**
 * @brief 마운트된 라우터에서 매칭 시도
 * @param method HTTP 메서드
 * @param path 요청 경로
 * @return 매칭 결과 (없으면 std::nullopt)
 */
[[nodiscard]] std::optional<RouteMatch> match_mounted_router(const std::string& method, const std::string& path) const;

/**
 * @brief 정규식 매칭 결과에서 파라미터 추출
 * @param match_result 정규식 매칭 결과
 * @param param_names 파라미터 이름들
 * @return 파라미터 맵
 */
[[nodiscard]] static std::unordered_map<std::string, std::string> 
    extract_parameters(const std::smatch& match_result, const std::vector<std::string>& param_names);
```

};

// ============================================================================
// 편의 함수들
// ============================================================================

/**

- @brief 간단한 핸들러 래퍼 (Context 없이 Request만 사용)
- @param handler Request만 받는 핸들러
- @return 표준 HandlerFunction
  */
  HandlerFunction simple_handler(std::function<Task<Response>(const Request&)> handler);

/**

- @brief 동기 핸들러 래퍼 (Task 없이 직접 Response 반환)
- @param handler 동기 핸들러
- @return 표준 HandlerFunction
  */
  HandlerFunction sync_handler(std::function<Response(Context&, const Request&)> handler);

/**

- @brief JSON 응답 전용 핸들러 래퍼
- @tparam T JSON 직렬화 가능한 타입
- @param handler JSON 객체를 반환하는 핸들러
- @return 표준 HandlerFunction
  */
  template<typename T>
  HandlerFunction json_handler(std::function<Task<T>(Context&, const Request&)> handler) {
  return [handler = std::move(handler)](Context& ctx, const Request& req) -> Task<Response> {
  auto result = co_await handler(ctx, req);
  // 실제 구현에서는 JSON 직렬화
  co_return Response::ok(”{}”).with_content_type(“application/json”);
  };
  }

/**

- @brief 정적 파일 서빙 핸들러 생성
- @param root_directory 정적 파일 루트 디렉토리
- @param enable_directory_listing 디렉토리 목록 표시 여부
- @return 정적 파일 핸들러
  */
  HandlerFunction static_file_handler(const std::string& root_directory,
  bool enable_directory_listing = false);

/**

- @brief 리다이렉트 핸들러 생성
- @param target_url 리다이렉트할 URL
- @param permanent 영구 리다이렉트 여부 (301 vs 302)
- @return 리다이렉트 핸들러
  */
  HandlerFunction redirect_handler(const std::string& target_url, bool permanent = false);

} // namespace stellane
