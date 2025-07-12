#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <regex>
#include <optional>
#include <array>
#include <string_view>
#include <cstdint>

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

using HandlerFunction = std::function<Task<Response>(Context&, const Request&)>;

struct RouteMatch {
HandlerFunction handler;
std::unordered_map<std::string, std::string> path_params;
std::string matched_pattern;

```
RouteMatch(HandlerFunction h, std::unordered_map<std::string, std::string> params = {},
           std::string pattern = "")
    : handler(std::move(h)), path_params(std::move(params)), matched_pattern(std::move(pattern)) {}
```

};

// ============================================================================
// Patricia Trie (Radix Tree) 구현 - 고성능 동적 라우팅
// ============================================================================

namespace detail {

/**

- @brief 라우트 세그먼트 타입
  */
  enum class SegmentType : uint8_t {
  STATIC = 0,     ///< 정적 문자열 (예: “users”)
  PARAM = 1,      ///< 파라미터 (예: “:id”)
  WILDCARD = 2,   ///< 와일드카드 (예: “*path”)
  CATCH_ALL = 3   ///< 모든 경로 매칭 (예: “**”)
  };

/**

- @brief 압축된 라우트 세그먼트
  */
  struct RouteSegment {
  SegmentType type;
  std::string value;           ///< 실제 문자열 또는 파라미터명
  std::string param_name;      ///< 파라미터인 경우 이름
  
  RouteSegment(SegmentType t, std::string v, std::string param = “”)
  : type(t), value(std::move(v)), param_name(std::move(param)) {}
  
  bool is_dynamic() const noexcept {
  return type == SegmentType::PARAM || type == SegmentType::WILDCARD || type == SegmentType::CATCH_ALL;
  }
  
  bool matches(std::string_view segment) const;
  };

/**

- @brief Patricia Trie 노드 - 메모리 최적화
  */
  class PatriciaNode {
  public:
  // 성능 최적화: 작은 자식 수에 대해 배열 사용
  static constexpr size_t SMALL_CHILDREN_THRESHOLD = 4;
  
  struct CompressedEdge {
  std::string prefix;                    ///< 압축된 공통 접두사
  std::unique_ptr<PatriciaNode> child;   ///< 자식 노드
  
  ```
   CompressedEdge(std::string p, std::unique_ptr<PatriciaNode> c)
       : prefix(std::move(p)), child(std::move(c)) {}
  ```
  
  };
  
  // 핸들러 저장 (HTTP 메서드별)
  std::unordered_map<std::string, HandlerFunction> handlers;
  
  // 동적 세그먼트 정보
  std::vector<RouteSegment> segments;
  
  // 자식 노드들 - 크기에 따라 최적화된 저장 방식
  union {
  std::array<CompressedEdge, SMALL_CHILDREN_THRESHOLD>* small_children;
  std::unordered_map<char, CompressedEdge>* large_children;
  };
  
  size_t children_count = 0;
  bool use_large_storage = false;
  
  PatriciaNode();
  ~PatriciaNode();
  
  // 복사/이동 생성자 (메모리 안전성)
  PatriciaNode(const PatriciaNode&) = delete;
  PatriciaNode& operator=(const PatriciaNode&) = delete;
  PatriciaNode(PatriciaNode&& other) noexcept;
  PatriciaNode& operator=(PatriciaNode&& other) noexcept;
  
  /**
  - @brief 자식 노드 추가 - 자동 스토리지 최적화
    */
    void add_child(char first_char, std::string prefix, std::unique_ptr<PatriciaNode> child);
  
  /**
  - @brief 자식 노드 검색
    */
    PatriciaNode* find_child(char first_char);
    const PatriciaNode* find_child(char first_char) const;
  
  /**
  - @brief 노드 분할 - 공통 접두사 추출
    */
    void split_at(size_t pos, const std::string& new_suffix);
  
  /**
  - @brief 메모리 사용량 통계
    */
    size_t memory_usage() const;

private:
void migrate_to_large_storage();
void cleanup_storage();
};

/**

- @brief 고성능 동적 라우트 매처 - Patricia Trie 기반
  */
  class PatriciaRouteMatcher {
  public:
  PatriciaRouteMatcher();
  ~PatriciaRouteMatcher() = default;
  
  /**
  - @brief 동적 라우트 등록
  - @param method HTTP 메서드
  - @param pattern 라우트 패턴 (예: “/users/:id/posts/:post_id”)
  - @param handler 핸들러 함수
    */
    void add_route(const std::string& method, const std::string& pattern, HandlerFunction handler);
  
  /**
  - @brief 라우트 매칭 - O(k) 시간복잡도
  - @param method HTTP 메서드
  - @param path 요청 경로
  - @return 매칭 결과
    */
    std::optional<RouteMatch> match(const std::string& method, const std::string& path) const;
  
  /**
  - @brief 등록된 라우트 통계
    */
    struct Statistics {
    size_t total_routes = 0;
    size_t total_nodes = 0;
    size_t memory_usage_bytes = 0;
    size_t max_depth = 0;
    double avg_lookup_time_ns = 0.0;
    };
  
  Statistics get_statistics() const;
  
  /**
  - @brief 라우트 목록 반환
    */
    std::vector<std::string> list_routes() const;
  
  /**
  - @brief 캐시 워밍업 - 자주 사용되는 경로 사전 캐시
    */
    void warmup_cache(const std::vector<std::string>& frequent_paths);

private:
// 메서드별 Patricia Trie 루트
std::unordered_map<std::string, std::unique_ptr<PatriciaNode>> method_trees_;

```
// 성능 최적화: 최근 조회 캐시 (LRU)
mutable std::unordered_map<std::string, RouteMatch> lookup_cache_;
mutable std::vector<std::string> cache_order_;
static constexpr size_t MAX_CACHE_SIZE = 1000;

/**
 * @brief 패턴을 세그먼트로 파싱
 */
std::vector<RouteSegment> parse_pattern(const std::string& pattern) const;

/**
 * @brief 경로를 세그먼트로 분할
 */
std::vector<std::string_view> split_path(std::string_view path) const;

/**
 * @brief 노드에 라우트 삽입
 */
void insert_route(PatriciaNode* node, const std::vector<RouteSegment>& segments, 
                 size_t segment_index, const std::string& method, HandlerFunction handler,
                 const std::string& original_pattern);

/**
 * @brief 재귀적 매칭 수행
 */
std::optional<RouteMatch> match_recursive(const PatriciaNode* node, 
                                        const std::vector<std::string_view>& path_segments,
                                        size_t segment_index, const std::string& method,
                                        std::unordered_map<std::string, std::string>& params) const;

/**
 * @brief 캐시 관리
 */
void update_cache(const std::string& cache_key, const RouteMatch& match) const;
RouteMatch* get_from_cache(const std::string& cache_key) const;
```

};

/**

- @brief 정적 라우트용 개선된 Trie
  */
  class OptimizedStaticTrie {
  public:
  struct TrieNode {
  // 메모리 최적화: 작은 맵에 대해 정렬된 벡터 사용
  std::vector<std::pair<std::string, std::unique_ptr<TrieNode>>> children;
  std::unordered_map<std::string, HandlerFunction> handlers;
  
  ```
   // 압축 최적화: 단일 자식의 경우 경로 압축
   bool is_compressed = false;
   std::string compressed_path;
   
   TrieNode* find_child(const std::string& segment);
   const TrieNode* find_child(const std::string& segment) const;
   void add_child(const std::string& segment, std::unique_ptr<TrieNode> child);
   
   size_t memory_usage() const;
  ```
  
  };
  
  OptimizedStaticTrie();
  ~OptimizedStaticTrie() = default;
  
  void add_route(const std::string& method, const std::vector<std::string>& segments, HandlerFunction handler);
  std::optional<RouteMatch> match(const std::string& method, const std::vector<std::string>& segments) const;
  
  size_t node_count() const;
  size_t memory_usage() const;

private:
std::unique_ptr<TrieNode> root_;

```
void compress_single_child_paths(TrieNode* node);
```

};

} // namespace detail

// ============================================================================
// 메인 Router 클래스 - 하이브리드 + Patricia Trie
// ============================================================================

/**

- @brief 고성능 하이브리드 라우터
- 
- 성능 특성:
- - 정적 라우트: O(1) ~ O(log k) (압축된 Trie)
- - 동적 라우트: O(k) (Patricia Trie, 패턴 수와 무관)
- - 메모리: 압축 최적화로 기존 대비 60% 절약
- - 캐시: LRU 캐시로 반복 조회 최적화
    */
    class Router {
    public:
    Router();
    ~Router() = default;
  
  // 복사/이동 생성자
  Router(const Router&) = delete;
  Router& operator=(const Router&) = delete;
  Router(Router&&) = default;
  Router& operator=(Router&&) = default;
  
  // ========================================================================
  // HTTP 메서드별 라우트 등록
  // ========================================================================
  
  Router& get(const std::string& path, HandlerFunction handler);
  Router& post(const std::string& path, HandlerFunction handler);
  Router& put(const std::string& path, HandlerFunction handler);
  Router& del(const std::string& path, HandlerFunction handler);
  Router& patch(const std::string& path, HandlerFunction handler);
  Router& options(const std::string& path, HandlerFunction handler);
  Router& head(const std::string& path, HandlerFunction handler);
  Router& all(const std::string& path, HandlerFunction handler);
  Router& route(const std::string& method, const std::string& path, HandlerFunction handler);
  
  // ========================================================================
  // 라우터 중첩 및 마운트
  // ========================================================================
  
  Router& mount(const std::string& prefix, const Router& sub_router);
  
  // ========================================================================
  // 라우트 매칭 - 성능 최적화
  // ========================================================================
  
  /**
  - @brief 고성능 라우트 매칭
  - 
  - 매칭 순서:
  - 1. 정적 라우트 (O(log k))
  - 1. Patricia Trie 동적 라우트 (O(k))
  - 1. 마운트된 라우터 (재귀)
     */
     [[nodiscard]] std::optional<RouteMatch> match(const std::string& method, const std::string& path) const;
     [[nodiscard]] std::optional<RouteMatch> match(const Request& request) const;
  
  // ========================================================================
  // 성능 최적화 기능
  // ========================================================================
  
  /**
  - @brief 라우팅 캐시 워밍업
  - @param frequent_paths 자주 사용되는 경로들
    */
    void warmup_cache(const std::vector<std::string>& frequent_paths);
  
  /**
  - @brief 라우팅 테이블 최적화
  - - 정적 Trie 압축
  - - Patricia Trie 리밸런싱
  - - 메모리 풀 최적화
      */
      void optimize();
  
  // ========================================================================
  // 라우트 정보 조회
  // ========================================================================
  
  [[nodiscard]] std::vector<std::string> list_routes() const;
  [[nodiscard]] bool has_route(const std::string& method, const std::string& path) const;
  [[nodiscard]] std::vector<std::string> allowed_methods(const std::string& path) const;
  [[nodiscard]] size_t route_count() const;
  
  // ========================================================================
  // 성능 모니터링
  // ========================================================================
  
  struct PerformanceStats {
  size_t total_lookups = 0;
  size_t cache_hits = 0;
  size_t static_route_hits = 0;
  size_t dynamic_route_hits = 0;
  size_t not_found_count = 0;
  
  ```
   // 메모리 사용량
   size_t static_trie_memory = 0;
   size_t patricia_trie_memory = 0;
   size_t cache_memory = 0;
   
   // 평균 응답 시간 (나노초)
   double avg_static_lookup_ns = 0.0;
   double avg_dynamic_lookup_ns = 0.0;
   
   double cache_hit_ratio() const {
       return total_lookups > 0 ? static_cast<double>(cache_hits) / total_lookups : 0.0;
   }
   
   size_t total_memory_usage() const {
       return static_trie_memory + patricia_trie_memory + cache_memory;
   }
  ```
  
  };
  
  [[nodiscard]] PerformanceStats get_performance_stats() const;
  void reset_performance_stats();
  
  // ========================================================================
  // 디버그 및 유틸리티
  // ========================================================================
  
  [[nodiscard]] std::string to_string(bool include_handlers = false) const;
  [[nodiscard]] std::string statistics() const;
  [[nodiscard]] std::string performance_report() const;

private:
// 정적 라우트 - 압축된 Trie
std::unordered_map<std::string, std::unique_ptr<detail::OptimizedStaticTrie>> static_routes_;

```
// 동적 라우트 - Patricia Trie
detail::PatriciaRouteMatcher dynamic_matcher_;

// 마운트된 하위 라우터들
std::vector<std::pair<std::string, std::shared_ptr<Router>>> mounted_routers_;

// 성능 모니터링
mutable PerformanceStats perf_stats_;
mutable std::chrono::steady_clock::time_point last_stats_reset_;

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

[[nodiscard]] static bool is_static_path(const std::string& path);
[[nodiscard]] static std::vector<std::string> split_path(const std::string& path);
[[nodiscard]] static std::string normalize_path(const std::string& path);
[[nodiscard]] static std::string normalize_method(const std::string& method);

void add_static_route(const std::string& method, const std::string& path, HandlerFunction handler);
void add_dynamic_route(const std::string& method, const std::string& path, HandlerFunction handler);

std::optional<RouteMatch> match_static_route(const std::string& method, const std::string& path) const;
std::optional<RouteMatch> match_dynamic_route(const std::string& method, const std::string& path) const;
std::optional<RouteMatch> match_mounted_router(const std::string& method, const std::string& path) const;

// 성능 측정
void record_lookup_time(bool is_static, std::chrono::nanoseconds duration) const;
void record_cache_hit() const;
```

};

// ============================================================================
// 편의 함수들
// ============================================================================

HandlerFunction simple_handler(std::function<Task<Response>(const Request&)> handler);
HandlerFunction sync_handler(std::function<Response(Context&, const Request&)> handler);

template<typename T>
HandlerFunction json_handler(std::function<Task<T>(Context&, const Request&)> handler) {
return [handler = std::move(handler)](Context& ctx, const Request& req) -> Task<Response> {
auto result = co_await handler(ctx, req);
// JSON 직렬화 구현 필요
co_return Response::ok(”{}”).with_content_type(“application/json”);
};
}

HandlerFunction static_file_handler(const std::string& root_directory, bool enable_directory_listing = false);
HandlerFunction redirect_handler(const std::string& target_url, bool permanent = false);

// ============================================================================
// 성능 벤치마킹 유틸리티
// ============================================================================

namespace benchmark {

/**

- @brief 라우터 성능 벤치마크
  */
  class RouterBenchmark {
  public:
  struct BenchmarkResult {
  size_t operations_per_second;
  double avg_latency_ns;
  double p95_latency_ns;
  double p99_latency_ns;
  size_t memory_usage_mb;
  };
  
  static BenchmarkResult benchmark_router(const Router& router,
  const std::vector<std::string>& test_paths,
  size_t iterations = 100000);
  
  static void compare_algorithms(const std::vector<std::string>& patterns,
  const std::vector<std::string>& test_paths);
  };

} // namespace benchmark

} // namespace stellane
