#include “stellane/core/task.h”
#include “stellane/core/context.h”
#include <thread>
#include <future>
#include <condition_variable>
#include <mutex>

namespace stellane {

// ============================================================================
// TaskMetadata 구현
// ============================================================================

std::chrono::milliseconds TaskMetadata::duration() const {
auto end_time = (state == TaskState::COMPLETED || state == TaskState::FAILED || state == TaskState::CANCELLED)
? completed_at
: std::chrono::steady_clock::now();

```
auto start_time = (state == TaskState::CREATED) ? created_at : started_at;

return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
```

}

bool TaskMetadata::is_finished() const noexcept {
return state == TaskState::COMPLETED ||
state == TaskState::FAILED ||
state == TaskState::CANCELLED;
}

// ============================================================================
// Task 구현
// ============================================================================

template<typename T>
void Task<T>::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept {
// 현재 Task가 완료되면 대기 중인 코루틴을 재개하도록 설정
promise().continuation_ = [awaiting_coroutine]() {
awaiting_coroutine.resume();
};

```
// Task가 아직 시작되지 않았다면 시작
if (promise().metadata_.state == TaskState::CREATED) {
    start();
}
```

}

template<typename T>
T Task<T>::await_resume() {
return promise().get_result();
}

template<typename T>
Task<T>& Task<T>::start() {
if (!handle_) {
return *this;
}

```
auto& meta = promise().metadata_;
if (meta.state == TaskState::CREATED) {
    meta.state = TaskState::RUNNING;
    meta.started_at = std::chrono::steady_clock::now();
    
    // 코루틴 시작
    if (!handle_.done()) {
        handle_.resume();
    }
}

return *this;
```

}

template<typename T>
Task<T>& Task<T>::cancel() {
if (handle_) {
promise().cancel();
}
return *this;
}

template<typename T>
T Task<T>::wait() {
if (!handle_) {
throw TaskExecutionException(“Invalid task handle”);
}

```
// Task 시작
start();

// 완료까지 대기 (블로킹)
std::mutex mutex;
std::condition_variable cv;
bool completed = false;

// 완료 콜백 설정
promise().continuation_ = [&mutex, &cv, &completed]() {
    std::lock_guard<std::mutex> lock(mutex);
    completed = true;
    cv.notify_one();
};

// 이미 완료되었는지 확인
if (is_done()) {
    return promise().get_result();
}

// 완료까지 대기
std::unique_lock<std::mutex> lock(mutex);
cv.wait(lock, [&completed] { return completed; });

return promise().get_result();
```

}

template<typename T>
std::optional<T> Task<T>::wait_for(std::chrono::milliseconds timeout) {
if (!handle_) {
return std::nullopt;
}

```
// Task 시작
start();

// 이미 완료되었는지 확인
if (is_done()) {
    try {
        if constexpr (std::is_void_v<T>) {
            promise().get_result();
            return std::make_optional<T>();
        } else {
            return promise().get_result();
        }
    } catch (...) {
        return std::nullopt;
    }
}

// 타임아웃과 함께 대기
std::mutex mutex;
std::condition_variable cv;
bool completed = false;

promise().continuation_ = [&mutex, &cv, &completed]() {
    std::lock_guard<std::mutex> lock(mutex);
    completed = true;
    cv.notify_one();
};

std::unique_lock<std::mutex> lock(mutex);
if (cv.wait_for(lock, timeout, [&completed] { return completed; })) {
    try {
        if constexpr (std::is_void_v<T>) {
            promise().get_result();
            return std::make_optional<T>();
        } else {
            return promise().get_result();
        }
    } catch (...) {
        return std::nullopt;
    }
}

return std::nullopt;
```

}

template<typename T>
bool Task<T>::has_result() const noexcept {
if (!handle_) return false;

```
const auto& meta = promise().metadata_;
return meta.state == TaskState::COMPLETED || meta.state == TaskState::FAILED;
```

}

template<typename T>
std::optional<T> Task<T>::try_get_result() const noexcept {
if (!has_result()) {
return std::nullopt;
}

```
try {
    if constexpr (std::is_void_v<T>) {
        promise().get_result();
        return std::make_optional<T>();
    } else {
        return promise().get_result();
    }
} catch (...) {
    return std::nullopt;
}
```

}

template<typename T>
Task<T>& Task<T>::with_name(std::string name) {
if (handle_) {
promise().metadata_.name = std::move(name);
}
return *this;
}

template<typename T>
std::chrono::milliseconds Task<T>::duration() const noexcept {
if (!handle_) {
return std::chrono::milliseconds::zero();
}

```
return promise().metadata_.duration();
```

}

template<typename T>
std::string Task<T>::to_string() const {
if (!handle_) {
return “Task[invalid]”;
}

```
const auto& meta = promise().metadata_;
std::ostringstream oss;

oss << "Task[";
if (!meta.name.empty()) {
    oss << "name=" << meta.name << ", ";
}

oss << "state=";
switch (meta.state) {
    case TaskState::CREATED: oss << "CREATED"; break;
    case TaskState::RUNNING: oss << "RUNNING"; break;
    case TaskState::SUSPENDED: oss << "SUSPENDED"; break;
    case TaskState::COMPLETED: oss << "COMPLETED"; break;
    case TaskState::FAILED: oss << "FAILED"; break;
    case TaskState::CANCELLED: oss << "CANCELLED"; break;
}

oss << ", duration=" << meta.duration().count() << "ms";
oss << "]";

return oss.str();
```

}

template<typename T>
Task<T>& Task<T>::with_context(Context& ctx) {
context_ = &ctx;
return *this;
}

// ============================================================================
// Task 체이닝 구현
// ============================================================================

template<typename T>
template<typename F>
auto Task<T>::then(F&& func) -> Task<std::invoke_result_t<F, T>> {
using ReturnType = std::invoke_result_t<F, T>;

```
if constexpr (std::is_void_v<T>) {
    co_await *this;
    if constexpr (std::is_void_v<ReturnType>) {
        func();
        co_return;
    } else {
        co_return func();
    }
} else {
    auto result = co_await *this;
    if constexpr (std::is_void_v<ReturnType>) {
        func(std::move(result));
        co_return;
    } else {
        co_return func(std::move(result));
    }
}
```

}

template<typename T>
template<typename F>
Task<T> Task<T>::catch_exception(F&& func) {
try {
co_return co_await *this;
} catch (…) {
auto exception = std::current_exception();
try {
if constexpr (std::is_void_v<T>) {
func(exception);
co_return;
} else {
co_return func(exception);
}
} catch (…) {
// 예외 처리 함수에서도 예외가 발생한 경우 원본 예외 다시 던지기
std::rethrow_exception(exception);
}
}
}

template<typename T>
template<typename F>
Task<T> Task<T>::finally(F&& func) {
try {
if constexpr (std::is_void_v<T>) {
co_await *this;
func();
co_return;
} else {
auto result = co_await *this;
func();
co_return result;
}
} catch (…) {
func();
throw;
}
}

// ============================================================================
// Task 유틸리티 함수들 구현
// ============================================================================

template<typename T>
Task<T> make_ready_task(T value) {
co_return value;
}

Task<void> make_ready_task() {
co_return;
}

template<typename T>
Task<T> make_failed_task(std::exception_ptr exception) {
std::rethrow_exception(exception);
co_return; // 절대 실행되지 않음
}

template<typename F>
auto make_task(F&& func) -> Task<std::invoke_result_t<F>> {
if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
func();
co_return;
} else {
co_return func();
}
}

// ============================================================================
// Task 조합 함수들 구현
// ============================================================================

template<typename… Tasks>
Task<std::tuple<typename Tasks::value_type...>> when_all(Tasks&&… tasks) {
// 모든 태스크를 병렬로 시작
(tasks.start(), …);

```
// 모든 태스크의 결과를 수집
if constexpr (sizeof...(tasks) == 0) {
    co_return std::tuple<>{};
} else {
    co_return std::make_tuple((co_await tasks)...);
}
```

}

template<typename T>
Task<std::vector<T>> when_all(std::vector<Task<T>> tasks) {
// 모든 태스크 시작
for (auto& task : tasks) {
task.start();
}

```
std::vector<T> results;
results.reserve(tasks.size());

// 모든 태스크의 결과 수집
for (auto& task : tasks) {
    if constexpr (std::is_void_v<T>) {
        co_await task;
    } else {
        results.push_back(co_await task);
    }
}

if constexpr (std::is_void_v<T>) {
    co_return;
} else {
    co_return results;
}
```

}

template<typename… Tasks>
Task<std::variant<typename Tasks::value_type...>> when_any(Tasks&&… tasks) {
// 구현 복잡성으로 인해 간단한 버전만 제공
// 실제로는 더 정교한 구현이 필요

```
// 모든 태스크 시작
(tasks.start(), ...);

// 첫 번째 태스크 결과 반환 (간단한 구현)
auto first_result = co_await std::get<0>(std::forward_as_tuple(tasks...));

using FirstType = typename std::tuple_element_t<0, std::tuple<Tasks...>>::value_type;
co_return std::variant<typename Tasks::value_type...>{first_result};
```

}

Task<void> sleep_for(std::chrono::milliseconds duration) {
// 간단한 구현 - 실제로는 이벤트 루프와 통합 필요
auto start = std::chrono::steady_clock::now();

```
while (std::chrono::steady_clock::now() - start < duration) {
    // 다른 태스크가 실행될 수 있도록 양보
    co_await std::suspend_always{};
    
    // 간단한 스핀 웨이트 (실제로는 타이머 이벤트 사용)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

co_return;
```

}

Task<void> sleep_until(std::chrono::steady_clock::time_point time_point) {
auto now = std::chrono::steady_clock::now();
if (time_point <= now) {
co_return;
}

```
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time_point - now);
co_return co_await sleep_for(duration);
```

}

template<typename T>
Task<T> with_timeout(Task<T> task, std::chrono::milliseconds timeout) {
// 원본 태스크와 타이머 태스크를 경쟁
task.start();

```
auto timeout_task = sleep_for(timeout);
timeout_task.start();

// 간단한 폴링 방식 구현 (실제로는 이벤트 기반으로 구현)
auto start_time = std::chrono::steady_clock::now();

while (!task.is_done()) {
    if (std::chrono::steady_clock::now() - start_time >= timeout) {
        task.cancel();
        throw TaskTimeoutException("Task timed out after " + std::to_string(timeout.count()) + "ms");
    }
    
    // 짧은 시간 대기
    co_await sleep_for(std::chrono::milliseconds(1));
}

if constexpr (std::is_void_v<T>) {
    co_await task;
    co_return;
} else {
    co_return co_await task;
}
```

}

// ============================================================================
// 명시적 템플릿 인스턴스화
// ============================================================================

// 자주 사용되는 타입들에 대한 명시적 인스턴스화
template class Task<void>;
template class Task<int>;
template class Task<std::string>;
template class Task<bool>;

// Response 타입 (forward declaration으로 인해 여기서는 주석 처리)
// template class Task<Response>;

// 유틸리티 함수들의 명시적 인스턴스화
template Task<int> make_ready_task<int>(int);
template Task<std::string> make_ready_task<std::string>(std::string);
template Task<bool> make_ready_task<bool>(bool);

template Task<int> make_failed_task<int>(std::exception_ptr);
template Task<std::string> make_failed_task<std::string>(std::exception_ptr);

template Task<std::vector<int>> when_all<int>(std::vector<Task<int>>);
template Task<std::vector<std::string>> when_all<std::string>(std::vector<Task<std::string>>);

template Task<int> with_timeout<int>(Task<int>, std::chrono::milliseconds);
template Task<std::string> with_timeout<std::string>(Task<std::string>, std::chrono::milliseconds);
template Task<void> with_timeout<void>(Task<void>, std::chrono::milliseconds);

// ============================================================================
// 런타임 통합 (향후 확장)
// ============================================================================

namespace detail {

/**

- @brief Task 스케줄링을 위한 간단한 실행기
- 실제로는 Stellane의 비동기 런타임과 통합되어야 함
  */
  class SimpleTaskExecutor {
  public:
  static SimpleTaskExecutor& instance() {
  static SimpleTaskExecutor executor;
  return executor;
  }
  
  template<typename T>
  void schedule(Task<T>& task) {
  // 간단한 스레드 풀에서 실행
  std::thread([&task]() {
  try {
  task.start();
  // Task 완료까지 대기하는 로직이 필요하지만
  // 여기서는 단순화
  } catch (…) {
  // 예외 처리
  }
  }).detach();
  }

private:
SimpleTaskExecutor() = default;
};

} // namespace detail

// ============================================================================
// 디버깅 및 모니터링 유틸리티
// ============================================================================

/**

- @brief 실행 중인 모든 Task의 상태를 추적하는 글로벌 레지스트리
  */
  class TaskRegistry {
  public:
  static TaskRegistry& instance() {
  static TaskRegistry registry;
  return registry;
  }
  
  template<typename T>
  void register_task(const Task<T>& task) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Task 등록 로직 (실제로는 약한 참조 사용)
  active_tasks_count_++;
  }
  
  template<typename T>
  void unregister_task(const Task<T>& task) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_tasks_count_ > 0) {
  active_tasks_count_–;
  }
  }
  
  size_t active_task_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_tasks_count_;
  }
  
  std::string statistics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return “Active tasks: “ + std::to_string(active_tasks_count_);
  }

private:
mutable std::mutex mutex_;
size_t active_tasks_count_ = 0;

```
TaskRegistry() = default;
```

};

// ============================================================================
// 전역 헬퍼 함수들
// ============================================================================

/**

- @brief 현재 실행 중인 Task 수 반환
  */
  size_t get_active_task_count() {
  return TaskRegistry::instance().active_task_count();
  }

/**

- @brief Task 시스템 통계 반환
  */
  std::string get_task_statistics() {
  return TaskRegistry::instance().statistics();
  }

} // namespace stellane
