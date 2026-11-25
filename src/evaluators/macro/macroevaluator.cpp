#include "macroevaluator.h"
#include "macro/pythonmacrojob.h"
#include "macro/pythonmacroservice.h"

MacroEvaluator::MacroEvaluator(PythonMacroService &service)
    : service(&service)
{}

void MacroEvaluator::setMacro(const QString &value)
{
    path = value;
}

EvaluationJob *MacroEvaluator::startEvaluation(const QString &input, QObject *parent)
{
    auto *job = new EvaluationJob(parent);
    if (!service || path.isEmpty()) {
        job->finish({false, "Select a Python macro in the toolbar"});
        return job;
    }

    auto *macro = new PythonMacroJob(*service, job);
    QObject::connect(job, &EvaluationJob::cancellationRequested, macro, &PythonMacroJob::cancel);
    QObject::connect(
        macro, &PythonMacroJob::completed, job, [job](const QString &text, const QString &error) {
            job->finish(error.isEmpty() ? Evaluation{true, text}
                                        : Evaluation{false, "Macro failed: " + error});
        });
    macro->start(path, input);
    return job;
}
