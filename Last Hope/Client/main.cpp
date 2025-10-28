#include "ClientApp.h"
#include <iostream>
using namespace std;

int main() {
    string username;
    cout << "Enter your username: ";
    cin >> username;
    cin.ignore();

    ClientApp app(username);
    app.start();
    return 0;
}
