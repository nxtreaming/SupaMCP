#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_json.h>
#include <mcp_json_utils.h>
#include <mcp_log.h>
#include <mcp_string_utils.h>

// Define API templates
#define API_USERS_LIST          "api://users"
#define API_USER_GET            "api://users/{user_id:int}"
#define API_USER_POSTS_LIST     "api://users/{user_id:int}/posts"
#define API_USER_POST_GET       "api://users/{user_id:int}/posts/{post_id:int}"
#define API_USER_POST_COMMENTS  "api://users/{user_id:int}/posts/{post_id:int}/comments"
#define API_SEARCH              "api://search/{query}/{page:int=1}/{limit:int=10}/{sort:pattern:date*=date-desc}"

// Mock database
typedef struct {
    int id;
    char* name;
    char* email;
} user_t;

typedef struct {
    int id;
    int user_id;
    char* title;
    char* content;
} post_t;

typedef struct {
    int id;
    int post_id;
    int user_id;
    char* content;
} comment_t;

// Mock data
user_t users[] = {
    {1, "John Doe", "john@example.com"},
    {2, "Jane Smith", "jane@example.com"},
    {3, "Bob Johnson", "bob@example.com"}
};

post_t posts[] = {
    {1, 1, "First Post", "This is the first post content."},
    {2, 1, "Second Post", "This is the second post content."},
    {3, 2, "Hello World", "This is Jane's first post."},
    {4, 3, "Introduction", "Hi, I'm Bob!"}
};

comment_t comments[] = {
    {1, 1, 2, "Great post, John!"},
    {2, 1, 3, "I agree with Jane."},
    {3, 2, 2, "Interesting thoughts."},
    {4, 3, 1, "Welcome, Jane!"},
    {5, 4, 1, "Nice to meet you, Bob!"}
};

// Helper functions
user_t* find_user_by_id(int user_id) {
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        if (users[i].id == user_id) {
            return &users[i];
        }
    }
    return NULL;
}

post_t* find_post_by_id(int post_id) {
    for (size_t i = 0; i < sizeof(posts) / sizeof(posts[0]); i++) {
        if (posts[i].id == post_id) {
            return &posts[i];
        }
    }
    return NULL;
}

// API handlers
char* handle_users_list(void) {
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_t* users_array = mcp_json_array_create();
    
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); i++) {
        mcp_json_t* user = mcp_json_object_create();
        mcp_json_object_set_property(user, "id", mcp_json_number_create(users[i].id));
        mcp_json_object_set_property(user, "name", mcp_json_string_create(users[i].name));
        mcp_json_object_set_property(user, "email", mcp_json_string_create(users[i].email));
        mcp_json_array_add_item(users_array, user);
    }
    
    mcp_json_object_set_property(response, "users", users_array);
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* handle_user_get(const mcp_json_t* params) {
    int user_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "user_id"));
    user_t* user = find_user_by_id(user_id);
    
    if (user == NULL) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("User not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_object_set_property(response, "id", mcp_json_number_create(user->id));
    mcp_json_object_set_property(response, "name", mcp_json_string_create(user->name));
    mcp_json_object_set_property(response, "email", mcp_json_string_create(user->email));
    
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* handle_user_posts_list(const mcp_json_t* params) {
    int user_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "user_id"));
    user_t* user = find_user_by_id(user_id);
    
    if (user == NULL) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("User not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_t* posts_array = mcp_json_array_create();
    
    for (size_t i = 0; i < sizeof(posts) / sizeof(posts[0]); i++) {
        if (posts[i].user_id == user_id) {
            mcp_json_t* post = mcp_json_object_create();
            mcp_json_object_set_property(post, "id", mcp_json_number_create(posts[i].id));
            mcp_json_object_set_property(post, "title", mcp_json_string_create(posts[i].title));
            mcp_json_object_set_property(post, "content", mcp_json_string_create(posts[i].content));
            mcp_json_array_add_item(posts_array, post);
        }
    }
    
    mcp_json_object_set_property(response, "user_id", mcp_json_number_create(user_id));
    mcp_json_object_set_property(response, "user_name", mcp_json_string_create(user->name));
    mcp_json_object_set_property(response, "posts", posts_array);
    
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* handle_user_post_get(const mcp_json_t* params) {
    int user_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "user_id"));
    int post_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "post_id"));
    
    user_t* user = find_user_by_id(user_id);
    if (user == NULL) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("User not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    post_t* post = find_post_by_id(post_id);
    if (post == NULL || post->user_id != user_id) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("Post not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_object_set_property(response, "id", mcp_json_number_create(post->id));
    mcp_json_object_set_property(response, "user_id", mcp_json_number_create(post->user_id));
    mcp_json_object_set_property(response, "user_name", mcp_json_string_create(user->name));
    mcp_json_object_set_property(response, "title", mcp_json_string_create(post->title));
    mcp_json_object_set_property(response, "content", mcp_json_string_create(post->content));
    
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* handle_user_post_comments(const mcp_json_t* params) {
    int user_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "user_id"));
    int post_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "post_id"));
    
    user_t* user = find_user_by_id(user_id);
    if (user == NULL) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("User not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    post_t* post = find_post_by_id(post_id);
    if (post == NULL || post->user_id != user_id) {
        mcp_json_t* error = mcp_json_object_create();
        mcp_json_object_set_property(error, "error", mcp_json_string_create("Post not found"));
        char* json = mcp_json_stringify(error);
        mcp_json_destroy(error);
        return json;
    }
    
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_t* comments_array = mcp_json_array_create();
    
    for (size_t i = 0; i < sizeof(comments) / sizeof(comments[0]); i++) {
        if (comments[i].post_id == post_id) {
            mcp_json_t* comment = mcp_json_object_create();
            mcp_json_object_set_property(comment, "id", mcp_json_number_create(comments[i].id));
            mcp_json_object_set_property(comment, "user_id", mcp_json_number_create(comments[i].user_id));
            
            user_t* commenter = find_user_by_id(comments[i].user_id);
            if (commenter != NULL) {
                mcp_json_object_set_property(comment, "user_name", mcp_json_string_create(commenter->name));
            }
            
            mcp_json_object_set_property(comment, "content", mcp_json_string_create(comments[i].content));
            mcp_json_array_add_item(comments_array, comment);
        }
    }
    
    mcp_json_object_set_property(response, "post_id", mcp_json_number_create(post_id));
    mcp_json_object_set_property(response, "post_title", mcp_json_string_create(post->title));
    mcp_json_object_set_property(response, "comments", comments_array);
    
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* handle_search(const mcp_json_t* params) {
    const char* query = mcp_json_string_value(mcp_json_object_get_property(params, "query"));
    int page = (int)mcp_json_number_value(mcp_json_object_get_property(params, "page"));
    int limit = (int)mcp_json_number_value(mcp_json_object_get_property(params, "limit"));
    const char* sort = mcp_json_string_value(mcp_json_object_get_property(params, "sort"));
    
    mcp_json_t* response = mcp_json_object_create();
    mcp_json_object_set_property(response, "query", mcp_json_string_create(query));
    mcp_json_object_set_property(response, "page", mcp_json_number_create(page));
    mcp_json_object_set_property(response, "limit", mcp_json_number_create(limit));
    mcp_json_object_set_property(response, "sort", mcp_json_string_create(sort));
    
    mcp_json_t* results = mcp_json_array_create();
    
    // Simple search implementation - just find posts containing the query string
    for (size_t i = 0; i < sizeof(posts) / sizeof(posts[0]); i++) {
        if (strstr(posts[i].title, query) != NULL || strstr(posts[i].content, query) != NULL) {
            mcp_json_t* result = mcp_json_object_create();
            mcp_json_object_set_property(result, "id", mcp_json_number_create(posts[i].id));
            mcp_json_object_set_property(result, "user_id", mcp_json_number_create(posts[i].user_id));
            
            user_t* author = find_user_by_id(posts[i].user_id);
            if (author != NULL) {
                mcp_json_object_set_property(result, "user_name", mcp_json_string_create(author->name));
            }
            
            mcp_json_object_set_property(result, "title", mcp_json_string_create(posts[i].title));
            mcp_json_object_set_property(result, "content", mcp_json_string_create(posts[i].content));
            mcp_json_array_add_item(results, result);
        }
    }
    
    mcp_json_object_set_property(response, "results", results);
    mcp_json_object_set_property(response, "total", mcp_json_number_create(mcp_json_array_get_size(results)));
    
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

// Router function
char* route_request(const char* uri) {
    // Try to match the URI against each template
    if (mcp_template_matches_optimized(uri, API_USERS_LIST)) {
        return handle_users_list();
    }
    
    if (mcp_template_matches_optimized(uri, API_USER_GET)) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, API_USER_GET);
        char* response = handle_user_get(params);
        mcp_json_destroy(params);
        return response;
    }
    
    if (mcp_template_matches_optimized(uri, API_USER_POSTS_LIST)) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, API_USER_POSTS_LIST);
        char* response = handle_user_posts_list(params);
        mcp_json_destroy(params);
        return response;
    }
    
    if (mcp_template_matches_optimized(uri, API_USER_POST_GET)) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, API_USER_POST_GET);
        char* response = handle_user_post_get(params);
        mcp_json_destroy(params);
        return response;
    }
    
    if (mcp_template_matches_optimized(uri, API_USER_POST_COMMENTS)) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, API_USER_POST_COMMENTS);
        char* response = handle_user_post_comments(params);
        mcp_json_destroy(params);
        return response;
    }
    
    if (mcp_template_matches_optimized(uri, API_SEARCH)) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, API_SEARCH);
        char* response = handle_search(params);
        mcp_json_destroy(params);
        return response;
    }
    
    // No match found
    mcp_json_t* error = mcp_json_object_create();
    mcp_json_object_set_property(error, "error", mcp_json_string_create("Not found"));
    char* json = mcp_json_stringify(error);
    mcp_json_destroy(error);
    return json;
}

// Helper function to print JSON with indentation
void print_json_response(const char* uri, const char* json) {
    printf("\nRequest: %s\n", uri);
    printf("Response:\n%s\n", json);
    printf("--------------------------------------------------\n");
}

// Example client function
void make_request(const char* uri) {
    char* response = route_request(uri);
    print_json_response(uri, response);
    free(response);
}

// URL builder function
char* build_url(const char* template_uri, mcp_json_t* params) {
    char* url = mcp_template_expand(template_uri, params);
    return url;
}

int main(int argc, char** argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;
    
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    
    printf("Template REST API Example\n");
    printf("========================\n\n");
    
    // Example 1: Get all users
    make_request("api://users");
    
    // Example 2: Get a specific user
    make_request("api://users/1");
    
    // Example 3: Get a non-existent user
    make_request("api://users/99");
    
    // Example 4: Get all posts for a user
    make_request("api://users/1/posts");
    
    // Example 5: Get a specific post
    make_request("api://users/1/posts/2");
    
    // Example 6: Get comments for a post
    make_request("api://users/1/posts/1/comments");
    
    // Example 7: Search with default parameters
    make_request("api://search/post");
    
    // Example 8: Search with custom parameters
    make_request("api://search/post/2/5/date-asc");
    
    // Example 9: Build and use a URL with parameters
    printf("\nURL Builder Example\n");
    printf("------------------\n");
    
    mcp_json_t* search_params = mcp_json_object_create();
    mcp_json_object_set_property(search_params, "query", mcp_json_string_create("Hello"));
    mcp_json_object_set_property(search_params, "page", mcp_json_number_create(3));
    mcp_json_object_set_property(search_params, "limit", mcp_json_number_create(15));
    mcp_json_object_set_property(search_params, "sort", mcp_json_string_create("date-asc"));
    
    char* search_url = build_url(API_SEARCH, search_params);
    printf("Built URL: %s\n", search_url);
    
    make_request(search_url);
    
    free(search_url);
    mcp_json_destroy(search_params);
    
    return 0;
}
