아래는 제공된 Stellane Router 사양 문서를 마크다운 형식으로 재작성한 결과입니다. 내용은 동일하며, 마크다운 문법을 사용하여 깔끔하게 구조화했습니다.
# Stellane Router Specification

Stellane Router는 C++20/23/26 기반의 **Stellane Ecosystem**의 핵심 라우팅 컴포넌트로, 게임 서버 및 실시간 애플리케이션을 위해 설계된 고성능 백엔드 프레임워크입니다. HTTP 요청 메서드(GET, POST 등)와 경로를 특정 핸들러 함수에 매핑하며, 정적 경로는 **Trie 자료구조**, 동적 경로는 **정규표현식**을 사용하는 하이브리드 매칭 전략으로 성능과 유연성을 보장합니다.

이 문서는 라우터의 아키텍처, 핵심 개념, 설정, 사용법, 타입-세이프 핸들러, 내장 메트릭스, WebSocket 지원 등을 다룹니다.

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
- [Router Configuration](#router-configuration)
  - [stellane.template.toml](#stellanetemplatetoml)
- [Core Concepts](#core-concepts)
  - [Handler Model (Sync & Async)](#handler-model-sync--async)
  - [Route Matching (Trie & Regex)](#route-matching-trie--regex)
  - [Dynamic Routes & Path Parameters](#dynamic-routes--path-parameters)
  - [WebSocket Routing](#websocket-routing)
- [Usage](#usage)
  - [C++ API](#c-api)
- [Thread Safety & Concurrency](#thread-safety--concurrency)
- [Performance & Caching](#performance--caching)
- [Built-in Metrics](#built-in-metrics)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [References](#references)
- [License](#license)

## Overview

Stellane Router는 들어오는 `Request`를 등록된 핸들러로 전달하는 디스패처입니다. 최소 오버헤드로 빠르고 정확한 요청 처리를 목표로 합니다.

- **C++20/23/26 Concepts**: `SyncHandler`와 `AsyncHandler` 개념으로 컴파일 타임에 핸들러 유효성을 검사하여 타입 안정성을 보장.
- **고성능 라우팅**: 정적 경로는 Trie를 통해 `O(L)` 시간 복잡도로 매칭(L=경로 길이). 동적 경로는 사전 컴파일된 정규표현식으로 처리.
- **타입 소거(Type Erasure)**: `HandlerWrapper`로 다양한 핸들러(람다, 함수 포인터 등)를 단일 컨테이너에 저장.
- **WebSocket 지원**: HTTP Upgrade 요청을 자동 감지하고 WebSocket 핸들러로 라우팅.
- **내장 메트릭스**: 요청 수, 성공/실패 수, 평균 응답 시간 등을 스레드-세이프하게 수집.

### Key Features
- **Performance**: 하이브리드 라우팅과 경로 캐싱으로 높은 처리량.
- **DX**: `router.get("/path", ...)` 등 직관적인 API.
- **Type Safety**: C++ Concepts로 컴파일 타임 검증.
- **Flexibility**: 동적 경로 파라미터(`/users/:id`) 지원.
- **Declarative Configuration**: TOML 파일로 선언적 라우트 정의.

## Architecture

라우터는 `Server`로부터 `Request`를 받아 `RouteMatcher`로 적절한 핸들러를 찾고, `HandlerWrapper`를 통해 실행 후 `Response`를 반환합니다.

### Architecture Diagram
[Server: Request Received] –> [Router::dispatch] | +–> [RouteMatcher::match_route] –(Path & Method)–> [Trie (Static) / Regex (Dynamic)] | | | (Handler Key) | | +–> [Handler Map::find] –(Handler Key)–> [HandlerWrapper] | | | (Execute Handler) | | +–> [Metrics::update] <———————– [Handler Logic] –> [Response]
### Components
- **Router**: 라우팅 로직의 메인 진입점. 핸들러 등록 및 요청 디스패치.
- **RouteMatcher**: HTTP 메서드와 경로로 핸들러 키를 찾음. Trie와 정규표현식 관리.
- **HandlerWrapper**: 동기/비동기 핸들러를 추상화하여 `handlers_` 맵에 저장.
- **RoutePattern**: 동적 경로를 파싱하여 정규표현식과 파라미터 이름을 저장.
- **Metrics**: `std::atomic`으로 락 없이 성능 지표 수집.

## Router Configuration

### stellane.template.toml
`stellane.template.toml` 파일로 선언적 경로 설정이 가능하며, `router.load_from_config()`로 활성화됩니다.

#### Schema (Router Section)
```toml
[router]
  [[router.routes]]
  method = "GET"
  path = "/api/v1/health"
  handler = "health_check_handler"

  [[router.routes]]
  method = "POST"
  path = "/api/v1/users"
  handler = "create_user_handler"

  [[router.websocket]]
  path = "/ws/chat"
  handler = "chat_websocket_handler"
handler 필드는 C++ 코드에서 핸들러를 식별하는 데 사용됩니다.
Core Concepts
Handler Model (Sync & Async)
C++ Concepts로 핸들러 시그니처를 강제하여 타입 안정성을 높입니다. 모든 핸들러는 std::expected를 반환.
	•	Syncintregrated SyncHandler: 동기적으로 std::expected 반환. std::expected my_handler(const Request& req);
	•	
	•	AsyncHandler: 비동기적으로 std::future> 반환. std::future> my_async_handler(const Request& req);
	•	
Route Matching (Trie & Regex)
	•	Static Routes: /api/users/all 같은 경로는 Trie로 O(L) 매칭.
	•	Dynamic Routes: /users/:id/profile 같은 경로는 사전 컴파일된 정규표현식으로 매칭.
Dynamic Routes & Path Parameters
콜론(:)으로 시작하는 경로 세그먼트는 동적 파라미터로 인식.
	•	정의: router.get("/articles/:category/:id", ...)
	•	요청: /articles/tech/123
	•	파라미터 추출: auto category = req.path_params["category"]; // "tech"
	•	auto id = req.path_params["id"]; // "123"
	•	
WebSocket Routing
request.is_websocket_upgrade()로 WebSocket 요청을 감지하고 WEBSOCKET 메서드 핸들러로 라우팅.
	•	등록: router.websocket("/ws/chat", ...)
	•	동작:
	◦	Upgrade 요청 수신.
	◦	/ws/chat 경로의 WEBSOCKET 핸들러 탐색.
	◦	Sec-WebSocket-Key로 Sec-WebSocket-Accept 헤더 생성.
	◦	상태 코드 101(Switching Protocols)로 핸드셰이크 완료.
Usage
C++ API
Router 클래스는 직관적인 HTTP 메서드 함수 제공.
#include "stellane/router.hpp"

int main() {
    stellane::Router router;

    // 1. 정적 경로 (동기)
    router.get("/api/health", [](const stellane::Request& req) -> std::expected {
        return stellane::Response::ok("{\"status\": \"ok\"}");
    });

    // 2. 동적 경로 (동기)
    router.get("/users/:id", [](const stellane::Request& req) -> std::expected {
        std::string user_id = req.path_params.at("id");
        return stellane::Response::ok("{\"user_id\": " + user_id + "}");
    });

    // 3. POST 요청
    router.post("/api/data", [](const stellane::Request& req) -> std::expected {
        return stellane::Response::ok("{\"received\": true}");
    });

    // 4. WebSocket 경로 (비동기)
    router.websocket("/ws", [](const stellane::Request& req) -> std::future> {
        return std::async([](){
            return stellane::Response{101};
        });
    });

    // Server에 주입
    // stellane::Server server(8080, ...);
    // server.route(router);
    // server.run();

    return 0;
}
Key Methods
Method
Description
get, post, put, del
동기 핸들러 등록
get_async 등
비동기 핸들러 등록
websocket
WebSocket 핸들러 등록
dispatch(Request&)
요청을 핸들러로 전달
get_metrics()
성능 지표 반환
load_from_config(path)
TOML 파일에서 라우트 구성 로드
korábban
System: You are Grok 3 built by xAI.
Thread Safety & Concurrency
라우터는 멀티-스레드 환경에서 안전하게 동작하도록 설계되었습니다.
	•	std::(shared_mutex): handlers_mutex_와 matcher_.mutex_를 사용하여 핸들러 및 라우트 정보 접근을 동기화. 라우트 등록 시 unique_lock, 요청 처리 시 shared_lock으로 동시성 극대화.
	•	std::atomic: 성능 메트릭스는 락 없이 원자적으로 업데이트.
Performance & Caching
	•	하이브리드 매칭: Trie와 정규표현식 조합으로 최적 성능.
	•	경로 캐시: RouteMatcher는 매칭된 경로 결과를 route_cache_에 저장, 동일 경로 요청 시 검색 생략.
Built-in Metrics
get_metrics()로 실시간 상태 모니터링 가능.
RouterMetrics Struct
struct RouterMetrics {
    uint64_t total_requests;      // 총 요청 수
    uint64_t successful_requests; // 성공 요청 수
    uint64_t failed_requests;     // 실패 요청 수
    uint64_t avg_response_time_ns; // 평균 응답 시간 (나노초)
    double success_rate;          // 성공률
};
Prometheus 같은 모니터링 시스템과 연동 가능.
Examples
기본 REST API 서버 라우팅 설정
#include "stellane/router.hpp"

void setup_user_routes(stellane::Router& router) {
    router.get("/users", [](const auto& req) {
        return stellane::Response::ok("[{\"id\": 1, \"name\": \"Alice\"}]");
    });

    router.get("/users/:id", [](const auto& req) {
        auto id = req.path_params.at("id");
        return stellane::Response::ok("{\"id\": " + id + "}");
    });

    router.post("/users", [](const auto& req) {
        return stellane::Response{201, {}, "{\"status\": \"created\"}"};
    });
}

int main() {
    stellane::Router main_router;
    setup_user_routes(main_router);
    // ... server.run(main_router) ...
}
Troubleshooting
Issue
Solution
404 Not Found
- 경로 등록 확인
- HTTP 메서드 일치 확인
- 동적 경로 파라미터 형식 확인
동적 경로 파라미터 비어 있음
- req.path_params.at("key") 사용으로 예외 발생 확인
WebSocket 핸드셰이크 실패
- router.websocket() 등록 확인
- Sec-WebSocket-Key 헤더 확인
컴파일 에러 (Concept)
- 핸들러 시그니처(std::expected 또는 std::future) 확인
References
	•	Stellane Server Specification
	•	C++20 Concepts
	•	Trie Data Structure
License
MIT License
이 마크다운 문서는 원본 내용을 그대로 유지하면서, 헤더, 리스트, 코드 블록, 테이블 등을 마크다운 문법에 맞게 정리했습니다. 추가 요청이 있으면 말씀해주세요!
