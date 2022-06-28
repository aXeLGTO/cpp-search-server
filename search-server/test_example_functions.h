#pragma once

#include "document.h"
#include "search_server.h"
#include "log_duration.h"

#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <map>
#include <random>
#include <iostream>
#include <utility>

template<typename T, typename U>
std::ostream& operator<<(std::ostream& os, const std::pair<T, U>& p) {
    using namespace std;
    return os << p.first << ": "s << p.second;
}

template<typename Container>
std::ostream& Print(std::ostream& os, const Container& container) {
    using namespace std;
    bool is_first = true;
    for (const auto& element : container) {
        if (!is_first) {
            os << ", "s;
        }

        is_first = false;
        os << element;
    }

    return os;
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
    os << '[';
    Print(os, v);
    return os << ']';
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::set<T>& s) {
    os << '{';
    Print(os, s);
    return os << '}';
}

template<typename K, typename V>
std::ostream& operator<<(std::ostream& os, const std::map<K, V>& m) {
    os << '{';
    Print(os, m);
    return os << '}';
}

template <typename QueriesProcessor>
void TestQuery(std::string_view mark, QueriesProcessor processor, const SearchServer& search_server, const std::vector<std::string>& queries) {
    LOG_DURATION(mark);
    const auto documents_lists = processor(search_server, queries);
}

#define TEST_QUERY(processor) TestQuery(#processor, processor, search_server, queries)

void PrintDocument(const Document& document);

void PrintMatchDocumentResult(int document_id, const std::vector<std::string>& words, DocumentStatus status);

void AddDocument(SearchServer& search_server, int document_id, const std::string& document,
                 DocumentStatus status, const std::vector<int>& ratings);

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query);

void TestRemoveDuplicates();

std::string GenerateWord(std::mt19937& generator, int max_length);

std::vector<std::string> GenerateDictionary(std::mt19937& generator, int word_count, int max_length);

std::string GenerateQuery(std::mt19937& generator, const std::vector<std::string>& dictionary, int max_word_count);

std::vector<std::string> GenerateQueries(std::mt19937& generator, const std::vector<std::string>& dictionary, int query_count, int max_word_count);

void TestBenchmarkQueries();

void TestBenchmarkQueriesJoined();

void TestRequests();

void TestMatchDocuments();

void TestExcludeStopWordsFromAddedDocumentContent();

void TestExcludeDocumentsWithMinusWords();

void TestNotMatchingDocumentsWithMinusWords();

void TestSortingDocumentsByRelevanceAndRating();

void TestCalculationOfRatingAddedDocuments();

void TestUserPredicateToFindDocuments();

void TestFindDocumentsWithStatus();

void TestCalculationOfRelevanceAddedDocuments();

void TestSearchServer();

int TestGeneral();

template <typename ExecutionPolicy>
void TestRemove(std::string_view mark, SearchServer search_server, ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    const int document_count = search_server.GetDocumentCount();
    for (int id = 0; id < document_count; ++id) {
        search_server.RemoveDocument(policy, id);
    }
    std::cout << search_server.GetDocumentCount() << std::endl;
}

#define TEST_REMOVE(mode) TestRemove(#mode, search_server, execution::mode)

