#ifndef REQUEST_LIMITER_H
#define REQUEST_LIMITER_H

class RequestLimiter {
private:
    static const unsigned long TIME_WINDOW = 60000; // 1 minute
    static const int MAX_REQUESTS = 60; // Max requests per minute
    static unsigned long requestTimes[];
    static int requestIndex;
        
public:
    static bool allowRequest();
    static void init();
    static bool allowTimeRequest();
};

#endif // REQUEST_LIMITER_H