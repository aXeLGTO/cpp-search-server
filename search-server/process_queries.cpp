#include "process_queries.h"
#include "document.h"
#include "search_server.h"

#include <vector>
#include <string>
#include <algorithm>
#include <execution>
#include <list>
#include <numeric>

using namespace std;

vector<vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const vector<string>& queries) {

    vector<vector<Document>> documents(queries.size());
    transform(
            execution::par,
            queries.begin(), queries.end(),
            documents.begin(),
            [&search_server](const string& query){
                return search_server.FindTopDocuments(query);
            });
    return documents;
}


list<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const vector<string>& queries) {

    return transform_reduce(
            execution::par,
            queries.begin(), queries.end(),
            list<Document>{},
            [](const auto& lhs_, const auto& rhs_) {
                auto lhs = const_cast<list<Document>&>(lhs_);
                auto rhs = const_cast<list<Document>&>(rhs_);
                lhs.splice(lhs.end(), rhs);
                return lhs;
            },
            [&search_server](const string& query) {
                const auto documents = search_server.FindTopDocuments(query);
                return list<Document>{documents.begin(), documents.end()};;
            });
}
