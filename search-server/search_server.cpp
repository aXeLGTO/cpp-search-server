#include "search_server.h"
#include <numeric>
#include <utility>

using namespace std;

SearchServer::SearchServer(string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {
}

SearchServer::SearchServer(const string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {
}

void SearchServer::AddDocument(int document_id, string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto words = SplitIntoWordsNoStop(document);
    const double inv_word_count = 1.0 / words.size();
    for (const auto& word : words) {
        auto it = word_to_document_freqs_.find(word);
        if (it == word_to_document_freqs_.end()) {
            it = word_to_document_freqs_.emplace(make_pair(string{word}, map<int, double>{})).first;
        }
        it->second[document_id] += inv_word_count;
        document_to_word_freqs_[document_id][it->first] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
    document_ids_.insert(document_id);
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(execution::seq, raw_query, status);
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query) const {
    return FindTopDocuments(execution::seq, raw_query);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(const string_view raw_query, int document_id) const {
    return MatchDocument(execution::seq, raw_query, document_id);
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(string_view word) {
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(string_view text) const {
    vector<string_view> words;
    for (const auto& word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + string{word} + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    return accumulate(ratings.begin(), ratings.end(), 0) / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    auto word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + string{text} + " is invalid"s);
    }
    return {word, is_minus, IsStopWord(word)};
}

SearchServer::NoUniqueQuery SearchServer::ParseNonUniqueQuery(string_view text) const {
    NoUniqueQuery result;
    for (const auto& word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    return result;
}

SearchServer::Query SearchServer::ParseQuery(string_view text) const {
    Query result;
    for (const auto& word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.insert(query_word.data);
            } else {
                result.plus_words.insert(query_word.data);
            }
        }
    }
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.find(word)->second.size());
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    if (document_to_word_freqs_.count(document_id)) {
        return document_to_word_freqs_.at(document_id);
    }

    static const map<string_view, double> m;
    return m;
}

void SearchServer::RemoveDocument(int document_id) {
    RemoveDocument(execution::seq, document_id);
}

void SearchServer::RemoveDocument(const std::execution::sequenced_policy& seq, int document_id) {
    for (const auto& [word, _] : document_to_word_freqs_[document_id]) {
        auto it = word_to_document_freqs_.find(word);
        if (it != word_to_document_freqs_.end()) {
            it->second.erase(document_id);
        }
    }
    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(const std::execution::parallel_policy& policy, int document_id) {
    using namespace std;

    if (document_ids_.count(document_id) == 0) {
        return;
    }

    const auto& words_in_document = document_to_word_freqs_[document_id];
    vector<string_view> words(words_in_document.size());
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
                word_to_document_freqs_.find(w)->second.erase(document_id);
            });
    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    document_ids_.erase(document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::sequenced_policy& seq, std::string_view raw_query, int document_id) const {
    using namespace std;

    const auto query = ParseQuery(raw_query);

    vector<string_view> matched_words;
    for (const auto& word : query.plus_words) {
        auto it = word_to_document_freqs_.find(word);
        if (it == word_to_document_freqs_.end()) {
            continue;
        }
        if (it->second.count(document_id)) {
            matched_words.push_back(it->first);
        }
    }

    for (const auto& word : query.minus_words) {
        auto it = word_to_document_freqs_.find(word);
        if (it == word_to_document_freqs_.end()) {
            continue;
        }
        if (it->second.count(document_id)) {
            static const vector<string_view> v;
            return {v, documents_.at(document_id).status};
        }
    }


    return {matched_words, documents_.at(document_id).status};
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy& policy, std::string_view raw_query, int document_id) const {
    using namespace std;

    if (document_ids_.count(document_id) == 0) {
        throw out_of_range("Document with id "s + to_string(document_id) + " not exist"s);
    }

    const auto& query = ParseNonUniqueQuery(raw_query);
    const auto& words_to_freqs = document_to_word_freqs_.find(document_id)->second;
    const auto& predicate = [&words_to_freqs](const auto& word){
        const auto& it = words_to_freqs.find(word);
        return it != words_to_freqs.end();
    };

    if (any_of(policy, query.minus_words.begin(), query.minus_words.end(), predicate)) {
        static const vector<string_view> v;
        return {v, documents_.at(document_id).status};
    }

    vector<string_view> matched_words(query.plus_words.size());
    transform(
        policy,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        [&words_to_freqs](const auto& word){
            const auto& it = words_to_freqs.find(word);
            return it != words_to_freqs.end() ? it->first : ""sv;
    });

    const auto& it = remove(policy, matched_words.begin(), matched_words.end(), ""sv);

    sort(policy, matched_words.begin(), it);
    matched_words.erase(
        unique(policy, matched_words.begin(), it),
        matched_words.end()
    );

    return {matched_words, documents_.at(document_id).status};
}
