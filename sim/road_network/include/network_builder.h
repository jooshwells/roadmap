#ifndef NETWORK_BUILDER_H
#define NETWORK_BUILDER_H

#include "network.h"

class NetworkBuilder {

    public:
        static Network buildNetworkFromJSONL(const std::string& nodePath, const std::string& edgePath);

};

#endif