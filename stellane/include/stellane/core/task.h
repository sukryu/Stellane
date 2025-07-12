#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <chrono>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

namespace stellane {

// Forward declarations
class Context;

// ============================================================================
// Task 상태 및 메타데이터
// ============================================================================

/**

- @brief Task 실행 상태
  */
  enum class TaskState : int {
  CREATED = 0,    ///< 생성됨 (아직 시작 안됨)
  RUNNING = 1,    ///< 실행 중
  SUSPENDED = 2,  ///< 일시 중지됨 (co_await 대기)
  COMPLETED = 3,  ///< 정상 완료
  FAILED = 4,     ///< 예외로 인한 실패
  CANCELLED = 5   ///< 취소됨
  };

/**

- @brief Task 메타데이터 (디버깅 및 모니터링용)
  */
  struct TaskMetadata {
  std::string name;                                           ///< Task 이름 (디버그용)
  std::chrono::steady_clock::time_point created_at;          ///< 생성 시간
  std::chrono::steady_clock::time_point started_at;          ///< 시작 시간
  std::chrono::steady_clock::time_point completed_at;        ///< 완료 시간
  TaskState state = TaskState::CREATED;                      ///< 현재 상태
  std::exception_ptr exception;                              ///< 발생한 예외 (있는 경우)
  
  TaskMetadata(std::string task_name = “”)
  : name(std::move(task_name))
  , created_at(std::chrono::steady_clock::now()) {}
  
  /**
  - @brief 실행 시간 계산
  - @return 실행 시간 (밀리초)
    */
    [[nodiscard]] std::chrono::milliseconds duration() const;
  
  /**
  - @brief Task가 완료되었는지 확인
  - @return 완료 여부 (성공 또는 실패)
    */
    [[nodiscard]] bool is_finished() const noexcept;
    };

// ============================================================================
// Task Promise 타입
// ============================================================================

/**

- @brief Task<T>의 코루틴 Promise 타입
- 
- C++20 코루틴의 핵심 인터페이스를 구현하여 Stellane의 비동기 실행 모델을 제공합니다.
  */
  template<typename T>
  struct TaskPromise {
  using value_type = T;
  using handle_type = std::coroutine_handle<TaskPromise<T>>;
  
  // ========================================================================
  // 코루틴 생명주기 관리
  // ========================================================================
  
  /**
  - @brief 코루틴 초기 상태 설정
  - @return 코루틴 즉시 시작하지 않음 (lazy evaluation)
    */
    std::suspend_always initial_suspend() noexcept { return {}; }
  
  /**
  - @brief 코루틴 완료 시 동작
  - @return 자동 정리하지 않음 (수동 관리)
    */
    std::suspend_always final_suspend() noexcept { return {}; }
  
  /**
  - @brief Task 객체 반환
  - @return 이 Promise를 래핑하는 Task
    */
    Task<T> get_return_object();
  
  /**
  - @brief 처리되지 않은 예외 처리
  - @param exception 발생한 예외
    */
    void unhandled_exception() noexcept;
  
  // ========================================================================
  // 값 반환 처리 (타입별 특수화)
  // ========================================================================
  
  /**
  - @brief co_return으로 값 반환 (T가 void가 아닌 경우)
  - @param value 반환할 값
    */
    template<typename U = T>
    requires (!std::is_void_v<U>)
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>) {
    result_.emplace(std::forward<U>(value));
    metadata_.state = TaskState::COMPLETED;
    metadata_.completed_at = std::chrono::steady_clock::now();
    
    // 대기 중인 continuation 실행
    if (continuation_) {
    continuation_();
    }
    }
  
  /**
  - @brief co_return으로 값 반환 (void 타입인 경우)
    */
    template<typename U = T>
    requires std::is_void_v<U>
    void return_void() noexcept {
    metadata_.state = TaskState::COMPLETED;
    metadata_.completed_at = std::chrono::steady_clock::now();
    
    // 대기 중인 continuation 실행
    if (continuation_) {
    continuation_();
    }
    }
  
  // ========================================================================
  // 내부 데이터
  // ========================================================================
  
  TaskMetadata metadata_;                                     ///< Task 메타데이터
  std::conditional_t<std::is_void_v<T>,
  std::monostate,
  std::optional<T>> result_;                ///< 결과 값 (void가 아닌 경우)
  std::function<void()> continuation_;                        ///< 완료 시 실행할 콜백
  std::atomic<bool> cancelled_{false};                       ///< 취소 플래그
  
  /**
  - @brief 결과 값 가져오기
  - @return 저장된 결과 값
  - @throws 예외가 저장되어 있으면 해당 예외
    */
    T get_result();
  
  /**
  - @brief Task 취소
    */
    void cancel() noexcept;
  
  /**
  - @brief 취소 여부 확인
  - @return 취소되었으면 true
    */
    [[nodiscard]] bool is_cancelled() const noexcept;
    };

// ============================================================================
// 메인 Task 클래스
// ============================================================================

/**

- @brief Stellane의 비동기 Task 래퍼
- 
- C++20 코루틴을 래핑하여 Stellane의 비동기 실행 모델을 제공합니다.
- 모든 핸들러와 미들웨어는 Task<T>를 반환해야 합니다.
- 
- 특징:
- - Lazy evaluation (명시적 시작 필요)
- - 취소 지원 (Cancellation Token)
- - 메타데이터 및 디버깅 정보
- - 체이닝 및 조합 가능
- - Context-aware 실행
- 
- @tparam T 반환 타입 (void 가능)
- 
- @example
- ```cpp
  
  ```
- Task<Response> handle_request(Context& ctx, const Request& req) {
- ```
  auto user_id = req.path_param<int>("id");
  ```
- ```
  if (!user_id) {
  ```
- ```
      co_return Response::bad_request("Invalid user ID");
  ```
- ```
  }
  ```
- 
- ```
  auto user = co_await database.fetch_user(*user_id);
  ```
- ```
  co_return Response::ok(user.to_json());
  ```
- }
- 
- // Task 조합
- Task<std::vector<User>> get_users_batch(const std::vector<int>& user_ids) {
- ```
  std::vector<Task<User>> tasks;
  ```
- ```
  for (auto id : user_ids) {
  ```
- ```
      tasks.push_back(database.fetch_user(id));
  ```
- ```
  }
  ```
- 
- ```
  auto users = co_await when_all(std::move(tasks));
  ```
- ```
  co_return users;
  ```
- }
- ```
  
  ```

*/
template<typename T = void>
class Task {
public:
using promise_type = TaskPromise<T>;
using handle_type = typename promise_type::handle_type;
using value_type = T;

```
// ========================================================================
// 생성자 및 소멸자
// ========================================================================

/**
 * @brief 코루틴 핸들로부터 Task 생성 (내부용)
 * @param handle 코루틴 핸들
 */
explicit Task(handle_type handle) noexcept 
    : handle_(handle) {}

/**
 * @brief 복사 생성자 (삭제됨)
 */
Task(const Task&) = delete;
Task& operator=(const Task&) = delete;

/**
 * @brief 이동 생성자
 * @param other 이동할 Task
 */
Task(Task&& other) noexcept 
    : handle_(std::exchange(other.handle_, {})) {}

/**
 * @brief 이동 대입 연산자
 * @param other 이동할 Task
 * @return *this
 */
Task& operator=(Task&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            handle_.destroy();
        }
        handle_ = std::exchange(other.handle_, {});
    }
    return *this;
}

/**
 * @brief 소멸자
 */
~Task() {
    if (handle_) {
        handle_.destroy();
    }
}

// ========================================================================
// Awaitable 인터페이스 (co_await 지원)
// ========================================================================

/**
 * @brief Task가 즉시 준비되었는지 확인
 * @return 항상 false (lazy evaluation)
 */
[[nodiscard]] bool await_ready() const noexcept {
    return handle_ && handle_.done();
}

/**
 * @brief 다른 코루틴에서 이 Task를 기다릴 때 호출
 * @param awaiting_coroutine 대기 중인 코루틴
 * @return 즉시 재개할지 여부
 */
void await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept;

/**
 * @brief co_await 완료 시 결과 반환
 * @return Task의 결과 값
 * @throws Task 실행 중 발생한 예외
 */
T await_resume();

// ========================================================================
// Task 실행 제어
// ========================================================================

/**
 * @brief Task 시작 (lazy evaluation에서 실제 실행)
 * @return *this (체이닝)
 */
Task& start();

/**
 * @brief Task 취소
 * @return *this (체이닝)
 */
Task& cancel();

/**
 * @brief Task 완료까지 대기 (블로킹)
 * @return Task의 결과 값
 * @throws Task 실행 중 발생한 예외
 */
T wait();

/**
 * @brief 타임아웃과 함께 Task 완료 대기
 * @param timeout 최대 대기 시간
 * @return 결과 값 (타임아웃 시 std::nullopt)
 */
std::optional<T> wait_for(std::chrono::milliseconds timeout);

/**
 * @brief Task가 완료되었는지 확인 (논블로킹)
 * @return 완료 여부
 */
[[nodiscard]] bool is_done() const noexcept;

/**
 * @brief Task가 취소되었는지 확인
 * @return 취소 여부
 */
[[nodiscard]] bool is_cancelled() const noexcept;

/**
 * @brief 현재 Task 상태 반환
 * @return Task 상태
 */
[[nodiscard]] TaskState state() const noexcept;

// ========================================================================
// 결과 처리
// ========================================================================

/**
 * @brief Task 결과가 준비되었는지 확인
 * @return 결과 준비 여부
 */
[[nodiscard]] bool has_result() const noexcept;

/**
 * @brief Task 결과 가져오기 (블로킹하지 않음)
 * @return 결과 값 (준비되지 않았으면 std::nullopt)
 */
std::optional<T> try_get_result() const noexcept;

// ========================================================================
// 메타데이터 및 디버깅
// ========================================================================

/**
 * @brief Task 메타데이터 반환
 * @return 메타데이터 참조
 */
[[nodiscard]] const TaskMetadata& metadata() const noexcept;

/**
 * @brief Task 이름 설정 (디버깅용)
 * @param name Task 이름
 * @return *this (체이닝)
 */
Task& with_name(std::string name);

/**
 * @brief Task 실행 시간 반환
 * @return 실행 시간 (완료되지 않았으면 현재까지의 시간)
 */
[[nodiscard]] std::chrono::milliseconds duration() const noexcept;

/**
 * @brief Task 정보를 문자열로 변환 (디버깅용)
 * @return 포맷된 Task 정보
 */
[[nodiscard]] std::string to_string() const;

// ========================================================================
// Task 조합 및 체이닝
// ========================================================================

/**
 * @brief Task 완료 후 다른 함수 실행 (then)
 * @tparam F 함수 타입
 * @param func 실행할 함수
 * @return 새로운 Task
 */
template<typename F>
auto then(F&& func) -> Task<std::invoke_result_t<F, T>>;

/**
 * @brief Task 실패 시 처리 함수 실행 (catch)
 * @tparam F 함수 타입
 * @param func 예외 처리 함수
 * @return 새로운 Task
 */
template<typename F>
Task<T> catch_exception(F&& func);

/**
 * @brief Task 완료 여부와 관계없이 실행 (finally)
 * @tparam F 함수 타입
 * @param func 정리 함수
 * @return 기존 Task
 */
template<typename F>
Task<T> finally(F&& func);

// ========================================================================
// Context 연동
// ========================================================================

/**
 * @brief Context와 함께 Task 실행
 * @param ctx 실행 컨텍스트
 * @return *this (체이닝)
 */
Task& with_context(Context& ctx);

/**
 * @brief 현재 연결된 Context 반환
 * @return Context 포인터 (없으면 nullptr)
 */
[[nodiscard]] Context* get_context() const noexcept;
```

private:
handle_type handle_;                                        ///< 코루틴 핸들
Context* context_ = nullptr;                               ///< 연결된 Context

```
/**
 * @brief Promise 객체 접근
 * @return Promise 참조
 */
[[nodiscard]] promise_type& promise() noexcept;
[[nodiscard]] const promise_type& promise() const noexcept;
```

};

// ============================================================================
// Task 유틸리티 함수들
// ============================================================================

/**

- @brief 완료된 Task 생성 (즉시 값 반환)
- @tparam T 반환 타입
- @param value 반환할 값
- @return 완료된 Task
  */
  template<typename T>
  Task<T> make_ready_task(T value);

/**

- @brief void Task 생성 (즉시 완료)
- @return 완료된 void Task
  */
  Task<void> make_ready_task();

/**

- @brief 실패한 Task 생성 (예외 포함)
- @tparam T 반환 타입
- @param exception 저장할 예외
- @return 실패한 Task
  */
  template<typename T>
  Task<T> make_failed_task(std::exception_ptr exception);

/**

- @brief 지연 실행 Task 생성
- @tparam F 함수 타입
- @param func 실행할 함수
- @return 지연 실행 Task
  */
  template<typename F>
  auto make_task(F&& func) -> Task<std::invoke_result_t<F>>;

// ============================================================================
// Task 조합 함수들
// ============================================================================

/**

- @brief 모든 Task가 완료될 때까지 대기
- @tparam Tasks Task 타입들
- @param tasks 대기할 Task들
- @return 모든 결과를 포함하는 tuple
  */
  template<typename… Tasks>
  Task<std::tuple<typename Tasks::value_type...>> when_all(Tasks&&… tasks);

/**

- @brief Task 벡터의 모든 원소가 완료될 때까지 대기
- @tparam T Task 결과 타입
- @param tasks Task 벡터
- @return 모든 결과를 포함하는 벡터
  */
  template<typename T>
  Task<std::vector<T>> when_all(std::vector<Task<T>> tasks);

/**

- @brief 임의의 Task 하나가 완료되면 반환
- @tparam Tasks Task 타입들
- @param tasks 경쟁할 Task들
- @return 첫 번째 완료된 Task의 결과
  */
  template<typename… Tasks>
  Task<std::variant<typename Tasks::value_type...>> when_any(Tasks&&… tasks);

/**

- @brief 지정된 시간만큼 대기
- @param duration 대기 시간
- @return void Task
  */
  Task<void> sleep_for(std::chrono::milliseconds duration);

/**

- @brief 특정 시점까지 대기
- @param time_point 대기할 시점
- @return void Task
  */
  Task<void> sleep_until(std::chrono::steady_clock::time_point time_point);

/**

- @brief Task에 타임아웃 추가
- @tparam T Task 결과 타입
- @param task 원본 Task
- @param timeout 타임아웃 시간
- @return 타임아웃이 적용된 Task
- @throws TimeoutException 타임아웃 발생 시
  */
  template<typename T>
  Task<T> with_timeout(Task<T> task, std::chrono::milliseconds timeout);

// ============================================================================
// 예외 타입들
// ============================================================================

/**

- @brief Task 취소 예외
  */
  class TaskCancelledException : public std::exception {
  public:
  explicit TaskCancelledException(const std::string& message = “Task was cancelled”)
  : message_(message) {}
  
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

/**

- @brief Task 타임아웃 예외
  */
  class TaskTimeoutException : public std::exception {
  public:
  explicit TaskTimeoutException(const std::string& message = “Task timed out”)
  : message_(message) {}
  
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

/**

- @brief Task 실행 예외
  */
  class TaskExecutionException : public std::exception {
  public:
  explicit TaskExecutionException(const std::string& message)
  : message_(message) {}
  
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

// ============================================================================
// 템플릿 구현 (inline)
// ============================================================================

template<typename T>
Task<T> TaskPromise<T>::get_return_object() {
return Task<T>{handle_type::from_promise(*this)};
}

template<typename T>
void TaskPromise<T>::unhandled_exception() noexcept {
metadata_.exception = std::current_exception();
metadata_.state = TaskState::FAILED;
metadata_.completed_at = std::chrono::steady_clock::now();

```
// 대기 중인 continuation 실행
if (continuation_) {
    continuation_();
}
```

}

template<typename T>
T TaskPromise<T>::get_result() {
if (metadata_.exception) {
std::rethrow_exception(metadata_.exception);
}

```
if (cancelled_.load()) {
    throw TaskCancelledException();
}

if constexpr (std::is_void_v<T>) {
    return;
} else {
    if (!result_.has_value()) {
        throw TaskExecutionException("Task completed without result");
    }
    return std::move(*result_);
}
```

}

template<typename T>
void TaskPromise<T>::cancel() noexcept {
cancelled_.store(true);
metadata_.state = TaskState::CANCELLED;
metadata_.completed_at = std::chrono::steady_clock::now();

```
if (continuation_) {
    continuation_();
}
```

}

template<typename T>
bool TaskPromise<T>::is_cancelled() const noexcept {
return cancelled_.load();
}

// Task 메서드들
template<typename T>
typename Task<T>::promise_type& Task<T>::promise() noexcept {
return handle_.promise();
}

template<typename T>
const typename Task<T>::promise_type& Task<T>::promise() const noexcept {
return handle_.promise();
}

template<typename T>
bool Task<T>::is_done() const noexcept {
return handle_ && handle_.done();
}

template<typename T>
bool Task<T>::is_cancelled() const noexcept {
return handle_ && promise().is_cancelled();
}

template<typename T>
TaskState Task<T>::state() const noexcept {
return handle_ ? promise().metadata_.state : TaskState::CREATED;
}

template<typename T>
const TaskMetadata& Task<T>::metadata() const noexcept {
static TaskMetadata empty_metadata;
return handle_ ? promise().metadata_ : empty_metadata;
}

template<typename T>
Context* Task<T>::get_context() const noexcept {
return context_;
}

} // namespace stellane
