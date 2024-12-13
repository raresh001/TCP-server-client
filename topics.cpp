#include <string.h>
#include <algorithm>
#include <iostream>

#include "topics.hpp"

using namespace std;

void topics_tree::subscribe(string& ID, const char* topic) {
    node* iter = root;

    while (*topic != '\0') {
        const char* next_part = strchr(topic, '/');
        if (next_part == nullptr) {
            next_part = strchr(topic, '\0');
        } else {
            next_part++;
        }

        // go to the correct child
        if (*topic == '*') {
            if (iter->child_asterisk == nullptr) {
                iter->child_asterisk = new node("*", iter);
            }

            iter = iter->child_asterisk;
        } else if (*topic == '+') {
            if (iter->child_plus == nullptr) {
                iter->child_plus = new node("+", iter);
            }

            iter = iter->child_plus;
        } else {
            string node_name;

            if (*next_part)
                node_name = string(topic, next_part - topic - 1);
            else
                node_name = string(topic);

            auto map_iterator = iter->children.find(node_name);

            if (map_iterator == iter->children.end()) {
                node* new_child = new node(node_name, iter);
                iter->children.insert({node_name, new_child});
                iter = new_child;
            } else {
                iter = map_iterator->second;
            }
        }

        topic = next_part;
    }

    // avoid adding duplicates to the subscribers vector
    if (find(iter->subscribers.begin(), iter->subscribers.end(), ID) 
                                                == iter->subscribers.end()) {
        iter->subscribers.push_back(ID);
    }
}

void topics_tree::unsubscribe(std::string& ID, const char* topic) {
    node* iter = root;

    while (*topic != '\0') {
        const char* next_part = strchr(topic, '/');
        if (next_part == nullptr) {
            next_part = strchr(topic, '\0');
        } else {
            next_part++;
        }

        // go to the correct child
        if (*topic == '*') {
            if (iter->child_asterisk == nullptr) {
                return;
            }

            iter = iter->child_asterisk;
        } else if (*topic == '+') {
            if (iter->child_plus == nullptr) {
                return;
            }

            iter = iter->child_plus;
        } else {
            string node_name;

            if (*next_part)
                node_name = string(topic, next_part - topic - 1);
            else
                node_name = string(topic);

            auto map_iterator = iter->children.find(node_name);

            if (map_iterator == iter->children.end()) {
                return;
            }

            iter = map_iterator->second;
        }

        topic = next_part;
    }

    auto subscriber = find(iter->subscribers.begin(), iter->subscribers.end(), ID);
    if (subscriber == iter->subscribers.end()) {
        return;
    }

    iter->subscribers.erase(subscriber);

    // delete all unnecessary nodes (with no children and no subscribers)
    while (iter != root
            && iter->children.empty()
            && iter->subscribers.empty()
            && iter->child_asterisk == nullptr
            && iter->child_plus == nullptr) {

        string name = iter->name;
        node* parent = iter->parent;
        delete iter;
        
        if (name == "*") {
            parent->child_asterisk = nullptr;
        } else if (name == "+") {
            parent->child_plus = nullptr;
        } else {
            parent->children.erase(name);
        }

        iter = parent;
    }
}

set<string>* topics_tree::get_subscribers(const char* topic) {
    set<string>* result = new set<string>();
    root->get_subscribers(*result, topic);
    return result;
}

void topics_tree::node::get_subscribers(set<string>& result, const char* topic) {
    if (*topic == '\0') {
        for (auto& subscriber : subscribers) {
            result.insert(subscriber);
        }

        return;
    }

    const char* next_part = strchr(topic, '/');
    if (next_part == nullptr) {
        next_part = strchr(topic, '\0');
    } else {
        next_part++;
    }

    if (name == "*") {
        // * should replace any number of points from path
        get_subscribers(result, next_part);
    }

    if (child_asterisk != nullptr) {
        child_asterisk->get_subscribers(result, next_part);
    }

    if (child_plus != nullptr) {
        child_plus->get_subscribers(result, next_part);
    }

    string node_name;
    if (*next_part)
        node_name = string(topic, next_part - topic - 1);
    else
        node_name = string(topic);

    auto map_iterator = children.find(node_name);
    if (map_iterator != children.end()) {
        map_iterator->second->get_subscribers(result, next_part);
    }
}

void topics_tree::delete_recursive(node* root) {
    if (root->child_asterisk) {
        delete_recursive(root->child_asterisk);
    }

    if (root->child_plus) {
        delete_recursive(root->child_plus);
    }

    for (auto& child : root->children) {
        delete_recursive(child.second);
    }

    delete root;
}
