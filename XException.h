//
//  XException.h
//  XExporter
//
//  Created by Oogh on 2020/3/19.
//  Copyright © 2020 Oogh. All rights reserved.
//

#ifndef XException_h
#define XException_h

#include <stdexcept>

class XException: public std::exception {
public:
    explicit XException(const char* what) {
        mWhat = what;
    }

    const char* what() const throw() {
        return mWhat;
    }

private:
    const char* mWhat;
};

#endif /* XException_h */
