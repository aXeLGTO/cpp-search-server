#include "process_queries.h"
#include "search_server.h"
#include "test_example_functions.h"
#include "document.h"

#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main() {
    TestSearchServer();
    cerr << "Search server testing finished"s << endl << endl;

    SearchServer search_server("and with"s);

    int id = 0;
    for (
        const string& text : {
            "funny pet and nasty rat"s,
            "funny pet with curly hair"s,
            "funny pet and not very nasty rat"s,
            "pet with rat and rat and rat"s,
            "nasty rat with curly hair"s,
        }
    ) {
        search_server.AddDocument(++id, text, DocumentStatus::ACTUAL, {1, 2});
    }

    const vector<string> queries = {
        "nasty rat -not"s,
        "not very funny nasty pet"s,
        "curly hair"s
    };

    cout << "* ProcessQueriesJoined *"s << endl;
    for (const Document& document : ProcessQueriesJoined(search_server, queries)) {
        cout << "Document "s << document.id << " matched with relevance "s << document.relevance << endl;
    }
    cout << endl;

    cout << "* ProcessQueries *"s << endl;
    for (const vector<Document>& documents : ProcessQueries(search_server, queries)) {
        for (const auto document : documents) {
            cout << "Document "s << document.id << " matched with relevance "s << document.relevance << endl;
        }
    }
    cout << endl;

    TestBenchmarkQueries();
    cout << endl;

    TestBenchmarkQueriesJoined();

    {
        mt19937 generator;
        const auto dictionary = GenerateDictionary(generator, 2'000, 25);
        const auto documents = GenerateQueries(generator, dictionary, 20'000, 10);

        SearchServer search_server(dictionary[0]);
        for (size_t i = 0; i < documents.size(); ++i) {
            search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, {1, 2, 3});
        }

        const auto queries = GenerateQueries(generator, dictionary, 2'000, 7);
        {
            LOG_DURATION("Trivial"s);
            vector<Document> documents_lists;
            for (const std::string& query : queries) {
                const auto documents = search_server.FindTopDocuments(query);
                for (const auto document : documents) {
                    documents_lists.push_back(document);
                }
            }
        }
    }

    return 0;
}
