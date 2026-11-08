#pragma once

#include "sanskrittypes.h"
#include <functional>
#include <utility>

namespace sanskrit {
// A move-only handle. Keep it until completion. Destruction cancels outstanding work.
class [[nodiscard]] Request
{
public:
    Request() = default;
    explicit Request(std::function<void()> cancel)
        : cancelAction(std::move(cancel))
    {}
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;
    Request(Request &&other) noexcept
        : cancelAction(std::exchange(other.cancelAction, {}))
    {}
    Request &operator=(Request &&other) noexcept
    {
        if (this != &other) {
            cancel();
            cancelAction = std::exchange(other.cancelAction, {});
        }
        return *this;
    }
    ~Request() { cancel(); }

    void cancel()
    {
        if (auto action = std::exchange(cancelAction, {}))
            action();
    }

private:
    std::function<void()> cancelAction;
};

template<typename Result>
using Completion = std::function<void(Result)>;

// Start requests, cancel them and deliver callbacks on one serialized execution context.
// A completion runs at most once, after the start call returns. Cancellation suppresses
// delivery and stops subsequent work. Cancel actions must not throw. The service must
// outlive its users and their requests.
class SanskritService
{
public:
    virtual ~SanskritService() = default;

    // Enqueue local completions on the same context as remote completions.
    virtual void post(std::function<void()> task) = 0;

    // Input is validated SLP1. includeBaseForms applies to a single headword.
    virtual Request lookup(
        std::vector<std::string> headwords,
        Dictionary dictionary,
        bool includeBaseForms,
        Completion<DictionaryResult> completion) = 0;

    virtual Request analyze(std::string text, Script script, Completion<Analysis> completion) = 0;
};
} // namespace sanskrit
