#pragma once

#include "evaluators/evaluator.h"
#include <functional>
#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>

namespace sanskrit {
struct SanskritEndpoints;
}

namespace lookup_test {
struct Reply
{
    QByteArray body;
    int status = 200;
    int delayMs = 0;
};

class LookupServer : public QTcpServer
{
public:
    QList<QUrl> requests;
    std::function<Reply(const QUrl &)> handler;

    LookupServer();

    sanskrit::SanskritEndpoints endpoints() const;
};

class DelayedEvaluator : public Evaluator
{
public:
    QList<QPointer<EvaluationJob>> jobs;

    EvaluationJob *startEvaluation(const QString &, QObject *parent) override;
};
} // namespace lookup_test

class LookupTests : public QObject
{
    Q_OBJECT

    QTemporaryDir settingsDirectory;

private slots:
    void initTestCase();

    void verseEvaluation();

    void workerCancellationAndRestart();

    void pythonInterpreterMenuAndPersistence();

    void transliteration_data();

    void transliteration();

    void invalidInput();

    void morphologyFixtures();

    void dictionaryFixtures();

    void invalidDictionaryResponses();

    void germanSubSensesAndPartialEntries();

    void subentryAndPreviousDefinitionFixtures();

    void responseLimits();

    void networkLookupAndLanguageSnapshot();

    void directAndMorphologicalMeanings();

    void noMatchAndPartialFailure();

    void reportedGermanWords_data();

    void reportedGermanWords();

    void kutumbakamLookup_data();

    void kutumbakamLookup();

    void recognizedBaseWithoutMeanings_data();

    void recognizedBaseWithoutMeanings();

    void referenceFailureKeepsOtherMeanings();

    void referenceChainIsBounded();

    void cancellingPendingReference();

    void inputValidationDoesNotUseNetwork();

    void cancellingLookupStopsDerivedRequests();

    void httpTimeoutLimitAndCancellation();

    void delayedReplacementUndoAndOtherEdits();

    void changedBlocksAndSupersededRequests();

    void independentBlocksAndEditorDestruction();

    void languageMenuAndPersistence();

    void sanskritWindowScreenshot();

    void liveCologne();
};
