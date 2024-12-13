#ifndef TOPICS_HPP
#define TOPICS_HPP

#include <string>
#include <vector>
#include <set>
#include <map>

class topics_tree {
public:
    topics_tree() { root = new node; }
    ~topics_tree() { if (root) delete_recursive(root); }

    // add the ID to the subscribers of the given topic
    void subscribe(std::string& ID, const char* topic);

    // unsubscribe ID from the given topic (nothing happens if
    // ID was not subscribed to that topic before)
    void unsubscribe(std::string& ID, const char* topic);

    // get a set of all subscribers from the given topic (including wildcards)
    std::set<std::string>* get_subscribers(const char* topic);

private:
    // tree node
    struct node {
        std::string name;
        std::vector<std::string> subscribers;

        // needs this parent for removing (unsubscribe)
        node* parent;

        // treat "*" and "+" separately since they will be requested each time
        node* child_asterisk;
        node* child_plus;
        std::map<std::string, node*> children;

        node(std::string name = std::string(), node* parent = nullptr) 
            : name(name), parent(parent), child_asterisk(nullptr), child_plus(nullptr) {}

        // search recursively through the tree for the given topic
        void get_subscribers(std::set<std::string>& result, const char* topic);
    };

    node* root;

    static void delete_recursive(node* root);
};

#endif  // TOPICS_HPP