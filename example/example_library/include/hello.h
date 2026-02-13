/**
 * This is an example of a header. Here we provide the definition for a function
 * in our corresponding source file. Note this file extension is ".h". You may
 * also use ".hpp" to denote specifically a C++ header file, but it does not 
 * change the way the files are compiled. Generally only use ".hpp" if you 
 * are using a C++ exclusive feature.
 * 
 * Note the #ifndef block at the top of the header. This is a classic way of
 * preventing multiple definition errors for larger C++ projects. Basically
 * the conditional block is saying the following: "Have I been here before? If
 * not, define the fact that I have been here before, and proceed with the function
 * definitions." If this header ends up included by the same file twice somehow, this
 * guard protects us against multiple definition errors.
 */

#ifndef HELLO_H
#define HELLO_H

#include <string>

void sayHello(const std::string& name);

#endif