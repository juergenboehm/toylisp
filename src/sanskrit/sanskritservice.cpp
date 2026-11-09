#include "sanskritservice.h"

namespace sanskrit {
Request::Request() noexcept = default;

Request::Request(std::function<void()> cancel)
    : cancelAction(std::move(cancel))
{}

Request::Request(Request &&other) noexcept
    : cancelAction(std::exchange(other.cancelAction, {}))
{}

Request &Request::operator=(Request &&other) noexcept
{
    if (this != &other) {
        cancel();
        cancelAction = std::exchange(other.cancelAction, {});
    }
    return *this;
}

Request::~Request()
{
    cancel();
}

void Request::cancel()
{
    if (auto action = std::exchange(cancelAction, {}))
        action();
}

SanskritService::~SanskritService() = default;
} // namespace sanskrit
