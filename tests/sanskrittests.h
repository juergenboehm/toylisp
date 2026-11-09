#pragma once

#include "sanskrit/sanskritservice.h"
#include <deque>

namespace sanskrit_test {
class TestService : public sanskrit::SanskritService
{
public:
    void post(std::function<void()> task) override;

    sanskrit::Request lookup(
        std::vector<std::string> headwords,
        sanskrit::Dictionary dictionary,
        bool includeBaseForms,
        sanskrit::Completion<sanskrit::DictionaryResult> completion) override;

    sanskrit::Request analyze(
        std::string text,
        sanskrit::Script script,
        sanskrit::Completion<sanskrit::Analysis> completion) override;

    void runNext();

    std::deque<std::function<void()>> tasks;
    sanskrit::Analysis analysis;
    sanskrit::DictionaryResult dictionaryResult;
    int lookups = 0;
    int analyses = 0;
    bool lastIncludeBaseForms = false;
    std::vector<std::string> lastHeadwords;
    std::string lastText;
    sanskrit::Script lastScript = sanskrit::Script::Devanagari;

private:
    template<typename Result>
    sanskrit::Request enqueue(sanskrit::Completion<Result> completion, Result result);
};
} // namespace sanskrit_test
