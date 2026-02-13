#include "hello.h"
#include <iostream>

void sayHello(const std::string& name)
{
    std::cout << "Hello, " << name << ". This is how we build with CMake!" << std::endl;
}

