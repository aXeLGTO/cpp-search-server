#pragma once

#include <vector>
#include <set>
#include <string>
#include <string_view>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <execution>
#include <functional>
#include <type_traits>
#include <iterator>
#include <future>
#include <atomic>

#include "string_processing.h"
#include "document.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double TOLERANCE = 1e-6;

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);

    explicit SearchServer(std::string_view stop_words_text);
    explicit SearchServer(const std::string& stop_words_text);

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename Policy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(Policy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;

    template <typename Policy>
    std::vector<Document> FindTopDocuments(Policy&& policy, std::string_view raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;

    template <typename Policy>
    std::vector<Document> FindTopDocuments(Policy&& policy, std::string_view raw_query) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;

    int GetDocumentCount() const;

    auto begin() const {
        return document_ids_.begin();
    }

    auto end() const {
        return document_ids_.end();
    }

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::sequenced_policy& seq, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::parallel_policy& par, std::string_view raw_query, int document_id) const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);
    void RemoveDocument(const std::execution::sequenced_policy& seq, int document_id);
    void RemoveDocument(const std::execution::parallel_policy& par, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string, std::map<int, double>, std::less<>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(std::string_view word) const;

    static bool IsValidWord(std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct NoUniqueQuery {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    struct Query {
        std::set<std::string_view> plus_words;
        std::set<std::string_view> minus_words;
    };

    Query ParseQuery(std::string_view text) const;
    NoUniqueQuery ParseNonUniqueQuery(std::string_view text) const;

    double ComputeWordInverseDocumentFreq(std::string_view word) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const std::execution::sequenced_policy& seq, const Query& query, DocumentPredicate document_predicate) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const std::execution::parallel_policy& par, NoUniqueQuery& query, DocumentPredicate document_predicate) const;
};

template <typename Policy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(Policy&& policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
    using namespace std;

    vector<Document> matched_documents;
    if constexpr (is_same_v<decay_t<Policy>, execution::parallel_policy>) {
        auto query = ParseNonUniqueQuery(raw_query);
        matched_documents = FindAllDocuments(policy, query, document_predicate);
    } else {
        matched_documents = FindAllDocuments(policy, ParseQuery(raw_query), document_predicate);
    }

    sort(policy, matched_documents.begin(), matched_documents.end(),
         [](const Document& lhs, const Document& rhs) {
             return lhs.relevance > rhs.relevance
                 || (abs(lhs.relevance - rhs.relevance) < TOLERANCE && lhs.rating > rhs.rating);
         });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}

template<typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(Policy&& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query,
        [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
}

template<typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(Policy&& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const std::execution::sequenced_policy& seq, const SearchServer::Query& query, DocumentPredicate document_predicate) const {
    using namespace std;

    map<int, double> document_to_relevance;
    for (const auto& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.find(word)->second) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }
    for (const auto& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.find(word)->second) {
            document_to_relevance.erase(document_id);
        }
    }

    vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back(
            {document_id, relevance, documents_.at(document_id).rating});
    }
    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const std::execution::parallel_policy& par, SearchServer::NoUniqueQuery& query, DocumentPredicate document_predicate) const {
    using namespace std;

    sort(par, query.plus_words.begin(), query.plus_words.end());
    query.plus_words.erase(
        unique(par, query.plus_words.begin(), query.plus_words.end()),
        query.plus_words.end()
    );

    ConcurrentMap<int, double> document_to_relevance(8);
    for_each(
            par,
            query.plus_words.begin(), query.plus_words.end(),
            [this, &document_to_relevance, document_predicate](const auto& word){
                if (word_to_document_freqs_.count(word) == 0) {
                    return;
                }
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                for (const auto [document_id, term_freq] : word_to_document_freqs_.find(word)->second) {
                    const auto& document_data = documents_.at(document_id);
                    if (document_predicate(document_id, document_data.status, document_data.rating)) {
                        document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                    }
                }
        });

    map<int, double> ordinary_map = document_to_relevance.BuildOrdinaryMap();
    for (const auto& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.find(word)->second) {
            ordinary_map.erase(document_id);
        }
    }

    vector<Document> matched_documents(ordinary_map.size());
    atomic_int size = 0;
    for_each(
        par,
        ordinary_map.begin(), ordinary_map.end(),
        [this, &matched_documents, &size](const auto& value) {
            const auto [document_id, relevance] = value;
            matched_documents[size++] = {document_id, relevance, documents_.at(document_id).rating};
        });
    matched_documents.resize(size);

    return matched_documents;
}

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
    if (!std::all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        throw std::invalid_argument("Some of stop words are invalid");
    }
}

