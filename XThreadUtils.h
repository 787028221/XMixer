//
//  XThreadUtils.h
//  XExporter
//
//  Created by Oogh on 2020/3/19.
//  Copyright © 2020 Oogh. All rights reserved.
//

#ifndef XEXPORTER_THREADUTILS_H
#define XEXPORTER_THREADUTILS_H

#include <pthread.h>

class XThreadUtils {
public:
    inline static void configThreadName(const char* name) {
#if __APPLE__
        pthread_setname_np(name);
#else
        pthread_setname_np(pthread_self(), name);
#endif
    }
};

#endif //XEXPORTER_THREADUTILS_H
