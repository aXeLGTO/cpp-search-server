#pragma once

#include <vector>
#include <set>
#include <string>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <execution>

#include "string_processing.h"
#include "document.h"

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

    explicit SearchServer(const std::string& stop_words_text);

    void AddDocument(int document_id, const std::string& document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(const std::string& raw_query, DocumentPredicate document_predicate) const;

    std::vector<Document> FindTopDocuments(const std::string& raw_query, DocumentStatus status) const;

    std::vector<Document> FindTopDocuments(const std::string& raw_query) const;

    int GetDocumentCount() const;

    auto begin() const {
        return document_ids_.begin();
    }

    auto end() const {
        return document_ids_.end();
    }

    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(const std::string& raw_query, int document_id) const;

    template<typename Policy>
    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(Policy&& policy, const std::string& raw_query, int document_id) const;

    const std::map<std::string, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);

    template<typename Policy>
    void RemoveDocument(Policy policy, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    const std::set<std::string> stop_words_;
    std::map<std::string, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(const std::string& word) const;

    static bool IsValidWord(const std::string& word);

    std::vector<std::string> SplitIntoWordsNoStop(const std::string& text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(const std::string& text) const;

    struct NoUniqueQuery {
        std::vector<std::string> plus_words;
        std::vector<std::string> minus_words;
    };

    struct Query {
        std::set<std::string> plus_words;
        std::set<std::string> minus_words;
    };

    Query ParseQuery(const std::string& text) const;
    NoUniqueQuery ParseNoUniqueQuery(const std::string& text) const;

    double ComputeWordInverseDocumentFreq(const std::string& word) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const;
};

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, DocumentPredicate document_predicate) const {
    using namespace std;

    const auto query = ParseQuery(raw_query);

    auto matched_documents = FindAllDocuments(query, document_predicate);

    sort(matched_documents.begin(), matched_documents.end(),
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
std::vector<Document> SearchServer::FindAllDocuments(const SearchServer::Query& query, DocumentPredicate document_predicate) const {
    using namespace std;

    map<int, double> document_to_relevance;
    for (const string& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }
    for (const string& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
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

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
    if (!std::all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        throw std::invalid_argument("Some of stop words are invalid");
    }
}

template<typename Policy>
void SearchServer::RemoveDocument(Policy policy, int document_id) {
    using namespace std;

    if (document_ids_.count(document_id) == 0) {
        return;
    }

    const auto& words_in_document = document_to_word_freqs_[document_id];
    vector<string> words(words_in_document.size());
    transform(
            policy,
            words_in_document.begin(), words_in_document.end(),
            words.begin(),
            [](const auto& p){
                return p.first;
            });
    for_each(
            policy,
            words.begin(), words.end(),
            [this, document_id](const auto& w) {
                word_to_document_freqs_[w].erase(document_id);
            });
    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    document_ids_.erase(document_id);
}

template<typename Policy>
std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(Policy&& policy, const std::string& raw_query, int document_id) const {
    using namespace std;

    if (document_ids_.count(document_id) == 0) {
        throw out_of_range("Document with id "s + to_string(document_id) + " not exist"s);
    }

    const auto query = ParseNoUniqueQuery(raw_query);

    if (any_of(
            policy,
            query.minus_words.begin(), query.minus_words.end(),
            [this, document_id](const auto& word){
                return word_to_document_freqs_.at(word).count(document_id);
            })) {
        static const vector<string> v;
        return {v, documents_.at(document_id).status};
    }

    vector<string> matched_words(query.plus_words.size());
    auto it = copy_if(
        policy,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        [document_id, this](const auto& word){
            return word_to_document_freqs_.at(word).count(document_id);
        });

    sort(policy, matched_words.begin(), it);
    matched_words.erase(
            unique(policy, matched_words.begin(), it),
            matched_words.end()
        );

    return {matched_words, documents_.at(document_id).status};
}
