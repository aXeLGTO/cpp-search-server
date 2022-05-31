#include "remove_duplicates.h"
#include <iostream>
#include <vector>
#include <set>
#include <map>

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
    map<set<string>, int> words_to_documents;
    vector<int> ids_to_remove;
    for (const int document_id : search_server) {
        set<string> words;
        for (const auto& [word, _] : search_server.GetWordFrequencies(document_id)) {
            words.insert(word);
        }

        if (words_to_documents.count(words)) {
            ids_to_remove.push_back(document_id);
        } else {
            words_to_documents[words] = document_id;
        }
    }

    for (const int id : ids_to_remove) {
        cout << "Found duplicate document id "s << id << endl;
        search_server.RemoveDocument(id);
    }
}
