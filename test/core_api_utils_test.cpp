#include <gtest/gtest.h>
#include "collection.h"
#include <vector>
#include <collection_manager.h>
#include <core_api.h>
#include "core_api_utils.h"

class CoreAPIUtilsTest : public ::testing::Test {
protected:
    Store *store;
    CollectionManager & collectionManager = CollectionManager::get_instance();
    std::atomic<bool> quit = false;

    std::vector<std::string> query_fields;
    std::vector<sort_by> sort_fields;

    void setupCollection() {
        std::string state_dir_path = "/tmp/typesense_test/core_api_utils";
        LOG(INFO) << "Truncating and creating: " << state_dir_path;
        system(("rm -rf "+state_dir_path+" && mkdir -p "+state_dir_path).c_str());

        store = new Store(state_dir_path);
        collectionManager.init(store, 1.0, "auth_key", quit);
        collectionManager.load(8, 1000);
    }

    virtual void SetUp() {
        setupCollection();
    }

    virtual void TearDown() {
        collectionManager.dispose();
        delete store;
    }
};


TEST_F(CoreAPIUtilsTest, StatefulRemoveDocs) {
    Collection *coll1;

    std::vector<field> fields = {field("title", field_types::STRING, false),
                                 field("points", field_types::INT32, false),};

    coll1 = collectionManager.get_collection("coll1").get();
    if(coll1 == nullptr) {
        coll1 = collectionManager.create_collection("coll1", 2, fields, "points").get();
    }

    for(size_t i=0; i<100; i++) {
        nlohmann::json doc;

        doc["id"] = std::to_string(i);
        doc["title"] = "Title " + std::to_string(i);
        doc["points"] = i;

        coll1->add(doc.dump());
    }

    bool done;
    deletion_state_t deletion_state;
    deletion_state.collection = coll1;
    deletion_state.num_removed = 0;

    // single document match

    coll1->get_filter_ids("points: 99", deletion_state.index_ids);
    for(size_t i=0; i<deletion_state.index_ids.size(); i++) {
        deletion_state.offsets.push_back(0);
    }

    stateful_remove_docs(&deletion_state, 5, done);
    ASSERT_EQ(1, deletion_state.num_removed);
    ASSERT_TRUE(done);

    // match 12 documents (multiple batches)
    for(auto& kv: deletion_state.index_ids) {
        delete [] kv.second;
    }
    deletion_state.index_ids.clear();
    deletion_state.offsets.clear();
    deletion_state.num_removed = 0;

    coll1->get_filter_ids("points:< 11", deletion_state.index_ids);
    for(size_t i=0; i<deletion_state.index_ids.size(); i++) {
        deletion_state.offsets.push_back(0);
    }

    stateful_remove_docs(&deletion_state, 4, done);
    ASSERT_EQ(4, deletion_state.num_removed);
    ASSERT_FALSE(done);

    stateful_remove_docs(&deletion_state, 4, done);
    ASSERT_EQ(8, deletion_state.num_removed);
    ASSERT_FALSE(done);

    stateful_remove_docs(&deletion_state, 4, done);
    ASSERT_EQ(11, deletion_state.num_removed);
    ASSERT_TRUE(done);

    // match 9 documents (multiple batches)
    for(auto& kv: deletion_state.index_ids) {
        delete [] kv.second;
    }
    deletion_state.index_ids.clear();
    deletion_state.offsets.clear();
    deletion_state.num_removed = 0;

    coll1->get_filter_ids("points:< 20", deletion_state.index_ids);
    for(size_t i=0; i<deletion_state.index_ids.size(); i++) {
        deletion_state.offsets.push_back(0);
    }

    stateful_remove_docs(&deletion_state, 7, done);
    ASSERT_EQ(7, deletion_state.num_removed);
    ASSERT_FALSE(done);

    stateful_remove_docs(&deletion_state, 7, done);
    ASSERT_EQ(9, deletion_state.num_removed);
    ASSERT_TRUE(done);

    // fetch raw document IDs
    for(size_t i=0; i<100; i++) {
        nlohmann::json doc;

        doc["id"] = std::to_string(i);
        doc["title"] = "Title " + std::to_string(i);
        doc["points"] = i;

        coll1->add(doc.dump());
    }

    for(auto& kv: deletion_state.index_ids) {
        delete [] kv.second;
    }
    deletion_state.index_ids.clear();
    deletion_state.offsets.clear();
    deletion_state.num_removed = 0;

    coll1->get_filter_ids("id:[0, 1, 2]", deletion_state.index_ids);
    for(size_t i=0; i<deletion_state.index_ids.size(); i++) {
        deletion_state.offsets.push_back(0);
    }

    stateful_remove_docs(&deletion_state, 5, done);
    ASSERT_EQ(3, deletion_state.num_removed);
    ASSERT_TRUE(done);

    // delete single doc

    for(auto& kv: deletion_state.index_ids) {
        delete [] kv.second;
    }
    deletion_state.index_ids.clear();
    deletion_state.offsets.clear();
    deletion_state.num_removed = 0;

    coll1->get_filter_ids("id: 10", deletion_state.index_ids);
    for(size_t i=0; i<deletion_state.index_ids.size(); i++) {
        deletion_state.offsets.push_back(0);
    }

    stateful_remove_docs(&deletion_state, 5, done);
    ASSERT_EQ(1, deletion_state.num_removed);
    ASSERT_TRUE(done);

    for(auto& kv: deletion_state.index_ids) {
        delete [] kv.second;
    }
    deletion_state.index_ids.clear();
    deletion_state.offsets.clear();
    deletion_state.num_removed = 0;

    // bad filter query
    auto op = coll1->get_filter_ids("bad filter", deletion_state.index_ids);
    ASSERT_FALSE(op.ok());
    ASSERT_STREQ("Could not parse the filter query.", op.error().c_str());

    collectionManager.drop_collection("coll1");
}

TEST_F(CoreAPIUtilsTest, MultiSearchEmbeddedKeys) {
    std::shared_ptr<http_req> req = std::make_shared<http_req>();
    std::shared_ptr<http_res> res = std::make_shared<http_res>(nullptr);

    req->params["filter_by"] = "user_id: 100";
    nlohmann::json body;
    body["searches"] = nlohmann::json::array();
    nlohmann::json search;
    search["collection"] = "users";
    search["filter_by"] = "age: > 100";
    body["searches"].push_back(search);

    req->body = body.dump();
    nlohmann::json embedded_params;
    embedded_params["filter_by"] = "foo: bar";
    req->embedded_params_vec.push_back(embedded_params);

    post_multi_search(req, res);

    // ensure that req params are appended to (embedded params are also rolled into req params)
    ASSERT_EQ("user_id: 100&&age: > 100&&foo: bar", req->params["filter_by"]);
}

TEST_F(CoreAPIUtilsTest, ExtractCollectionsFromRequestBody) {
    std::map<std::string, std::string> req_params;
    std::string body = R"(
      {
        "name": "coll1",
        "fields": [
          {"name": "title", "type": "string" },
          {"name": "points", "type": "int32" }
        ],
        "default_sorting_field": "points"
      }
    )";

    route_path rpath("POST", {"collections"}, post_create_collection, false, false);
    std::vector<collection_key_t> collections;

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("coll1", collections[0].collection);
    ASSERT_EQ("foo", collections[0].api_key);

    // badly constructed collection schema body
    collections.clear();
    body = R"(
      {
        "name": "coll1
        "fields": [
          {"name": "title", "type": "string" },
          {"name": "points", "type": "int32" }
        ],
        "default_sorting_field": "points"
      }
    )";

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);
    ASSERT_EQ("foo", collections[0].api_key);

    collections.clear();
    // missing collection name
    body = R"(
      {
        "fields": [
          {"name": "title", "type": "string" },
          {"name": "points", "type": "int32" }
        ],
        "default_sorting_field": "points"
      }
    )";

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);
    ASSERT_EQ("foo", collections[0].api_key);

    // check for multi_search
    collections.clear();
    rpath = route_path("GET", {"collections"}, post_multi_search, false, false);
    body = R"(
        {"searches":[
              {
                "query_by": "concat",
                "collection": "products",
                "q": "battery",
                "x-typesense-api-key": "bar"
              }
          ]
        }
    )";

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("products", collections[0].collection);
    ASSERT_EQ("bar", collections[0].api_key);

    // get collection for multi-search
    collections.clear();
    body = R"(
        {"searches":
              {
                "query_by": "concat",
                "collection": "products",
                "q": "battery"
              }
          ]
        }
    )";

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);
    ASSERT_EQ("foo", collections[0].api_key);

    collections.clear();
    body = R"(
        {"searches":[
              {
                "query_by": "concat",
                "q": "battery",
                "x-typesense-api-key": "bar"
              }
          ]
        }
    )";

    get_collections_for_auth(req_params, body, rpath, "foo", collections);
    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);
    ASSERT_EQ("bar", collections[0].api_key);
}

TEST_F(CoreAPIUtilsTest, ExtractCollectionsFromRequestBodyExtended) {
    route_path rpath_multi_search = route_path("POST", {"multi_search"}, post_multi_search, false, false);
    std::map<std::string, std::string> req_params;

    std::vector<collection_key_t> collections;
    get_collections_for_auth(req_params, "{]", rpath_multi_search, "", collections);

    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);

    nlohmann::json sample_search_body;
    sample_search_body["searches"] = nlohmann::json::array();
    nlohmann::json search_query;
    search_query["q"] = "aaa";
    search_query["collection"] = "company1";

    sample_search_body["searches"].push_back(search_query);

    search_query["collection"] = "company2";
    sample_search_body["searches"].push_back(search_query);

    collections.clear();
    get_collections_for_auth(req_params, sample_search_body.dump(), rpath_multi_search, "", collections);

    ASSERT_EQ(2, collections.size());
    ASSERT_EQ("company1", collections[0].collection);
    ASSERT_EQ("company2", collections[1].collection);

    collections.clear();
    req_params["collection"] = "foo";

    get_collections_for_auth(req_params, sample_search_body.dump(), rpath_multi_search, "", collections);

    ASSERT_EQ(2, collections.size());
    ASSERT_EQ("company1", collections[0].collection);
    ASSERT_EQ("company2", collections[1].collection);

    collections.clear();

    // when one of the search arrays don't have an explicit collection, use the collection name from req param
    sample_search_body["searches"][1].erase("collection");

    get_collections_for_auth(req_params, sample_search_body.dump(), rpath_multi_search, "", collections);

    ASSERT_EQ(2, collections.size());
    ASSERT_EQ("company1", collections[0].collection);
    ASSERT_EQ("foo", collections[1].collection);

    collections.clear();
    req_params.clear();

    route_path rpath_search = route_path("GET", {"collections", ":collection", "documents", "search"}, get_search, false, false);
    get_collections_for_auth(req_params, sample_search_body.dump(), rpath_search, "", collections);

    ASSERT_EQ(1, collections.size());
    ASSERT_EQ("", collections[0].collection);
}
