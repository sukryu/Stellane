🧠 Async Runtime in Stellane

> An internal view of how Stellane executes tasks through a cross-platform, fault-tolerant asynchronous runtime engine.

⸻

## 1. 개요 (Overview)

Stellane의 비동기 런타임은 Task<> 기반 코루틴을 **이벤트 루프(Event Loop)**로 실행시키는 핵심 엔진입니다.
모든 HTTP 요청, 미들웨어, 핸들러, 데이터베이스 작업은 이 런타임을 통해 논블로킹 방식으로 순차적으로 처리됩니다.

Stellane은 다음과 같은 2단계 이벤트 루프 구조를 지원합니다:

구조	설명	대표 백엔드
🧵 단일 루프	싱글 스레드에서 모든 코루틴을 관리	epoll, libuv
🧵🧵 멀티 루프	여러 워커 스레드에서 이벤트 루프를 분산 실행	io_uring, custom backend


⸻

## 2. 설계 철학 (Design Philosophy)
  •	Unifex 기반 경량 코루틴 모델
	•	백엔드 플러그인 선택(libuv/io_uring) + Stellane 자체 루프 지원
	•	컨텍스트(Context) 및 트레이싱(Trace) 안전 전파
	•	장애 발생 시 루프 상태 복원 기능 지원 (옵션)

⸻

## 3. 아키텍처 구성 (Runtime Architecture)

┌──────────────────────────────┐
│         Server (main)        │
├──────────────────────────────┤
│ Router → Middleware → Handler│
│      (All return Task<>)     │
├──────────────────────────────┤
│  AsyncRuntime (Event Engine) │
│     ┌────────────┐           │
│     │ EventLoop  │<──────────── epoll/io_uring/libuv
│     └────────────┘           │
├──────────────────────────────┤
│  Coroutine Scheduler + Queue │
│  Worker Pool (optional)      │
└──────────────────────────────┘


⸻

4. 이벤트 루프 백엔드 (Event Loop Backends)

Stellane은 아래와 같은 추상 인터페이스를 통해 다양한 런타임을 지원합니다:

class IEventLoopBackend {
public:
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void schedule(Task<> task) = 0;
    virtual ~IEventLoopBackend() = default;
};

✅ 지원 백엔드 종류

Backend	설명	플랫폼
EpollBackend	기본 싱글 스레드 이벤트 루프	Linux
LibUVBackend	libuv 기반 이벤트 루프	Linux/macOS/Windows
IoUringBackend	io_uring 기반 멀티 루프	Linux only
StellaneRuntime (WIP)	커스텀 멀티 이벤트 루프 (스케줄러 내장)	모든 OS

// 선택적 설정: stellane.template.toml
[runtime]
backend = "libuv"   # or "io_uring", "custom"


⸻

5. 멀티 이벤트 루프 지원 (Multi-Loop Execution)

멀티 루프는 아래 구조로 동작합니다:

Main Thread
 ├─ Accept Socket
 └─ Dispatcher
     ├── Worker 1: EventLoop
     ├── Worker 2: EventLoop
     └── Worker N: EventLoop

	•	각 워커는 CPU core pinning을 고려한 단일 루프
	•	요청은 round-robin / affinity 기반으로 분산 처리
	•	각 루프는 자체 Task 큐와 scheduler를 갖음

⸻

6. 장애 복구 전략 (Runtime Fault Recovery)

💥 문제 상황

서버가 예상치 못한 종료(segfault, panic, kill -9 등)로 인해
이벤트 루프가 비정상 종료된 경우에도 요청 처리 중이던 일부 정보나 상태는 복구 가능해야 합니다.

🔐 Stellane의 복구 구조

┌───────────────────────────────┐
│      Loop Recovery Layer      │
├───────────────────────────────┤
│ 1. Persistent Request Queue   │ ← [파일 기반 또는 mmap]
│ 2. Trace ID + Metadata 저장   │ ← [trace_id, timestamp, path 등]
│ 3. Recovery Hook 등록 가능    │ ← on_recover(ctx, Request)
└───────────────────────────────┘

✅ 핵심 기능

기능	설명
🧠 요청 재생성	마지막 처리 중이던 요청 정보 디스크에 기록
🧠 Trace 재연결	기존 Trace ID로 다시 로깅 시스템에 연결
🔄 복구 핸들러	사용자가 직접 on_recover() 핸들러 등록 가능
💾 저장소 선택	mmap, LevelDB, RocksDB 등 pluggable backend 예정

server.enable_request_recovery();

server.on_recover([](Context& ctx, const Request& req) -> Task<> {
    ctx.log("Recovered request: " + req.path());
    co_await retry_logic(req);
});

⚠️ 제한 사항
	•	POST body payload가 크거나 비동기 I/O 중이면 일부 복구 어려움
	•	TLS 복호화 상태는 복구 대상이 아님 (TCP/IP raw 상태만 저장 가능)

⸻

7. 향후 계획 (Future Extensions)

항목	상태	설명
AsyncRuntime abstraction	✅ 완료	IEventLoopBackend 추상화
libuv / io_uring 지원	✅ 진행 중	epoll, uring, uv 선택 가능
멀티 루프 작업 분산	🟡	CPU core 기반 루프 분산 및 Task 큐
요청 복구 시스템	🟡	metadata + 미들웨어 상태 저장
상태 저장 연동	🟡	Redis/RocksDB 연동 옵션화 예정
Runtime 분석 툴 (stellane analyze)	🔜	루프별 처리량, 처리 지연 분석 UI 제공


⸻

8. 참고 자료
	•	concepts/context.md – 컨텍스트 전파 방식
	•	internals/routing_tree.md – 핸들러 매핑 구조
	•	reference/runtime.md (작성 예정) – 런타임 구성 API
	•	io_uring 문서 – Linux 고성능 I/O

⸻
