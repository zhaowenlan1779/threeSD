// Dummy

#include <iostream>
#include "common/logging/log.h"

int main() {

    LOG_ERROR(Frontend, "test");
    _sleep(1000);
    LOG_WARNING(Frontend, "test2");
    system("pause");

    return 0;
}