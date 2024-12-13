#include <iostream>
#include <stdio.h>
#include "server.hpp"
#include "utils.h"

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        // Wrong call of server: it should be ./server <IP_PORT>
        return 1;
    }

    // unbuffer STDOUT
    DIE(setvbuf(stdout, NULL, _IONBF, BUFSIZ), "Unbuffering STDOUT failed");

    try {
        server* Server = new server(atoi(argv[1]));
        Server->run();
        delete Server;
    } catch (exception& e) {
        cerr << "ERROR OCCURED: " << e.what() << endl;
        return 1;
    }

    return 0;
}
