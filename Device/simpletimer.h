
#ifndef SIMPLE_TIMER_H
#define SIMPLE_TIMER_H

class SimpleTimer {
public:
    SimpleTimer() 
    {
        started_ = false;
    }
    void start() 
    {
        started_ = true;
        start_ = now();
    }
    void stop() 
    {
        started_ = false;
        end_ = now();
    }
    double seconds() 
    {
        auto diff = static_cast<double>(end() - start_);
        return  diff / 1000.0;
    }
    double milliseconds() 
    {
        return static_cast<double>(end() - start_);
    }
    bool started() 
    {
        return started_;
    }
    static void init() {
        SystemTickCounterInit();
    }
private:
    uint64_t now() {
        return SystemTickCounterRead();
    }
    uint64_t end() {
        if (started_) {
            // not stopped yet, so return "elapsed time so far".
            end_ = SystemTickCounterRead();
        }
        return end_;
    }
    uint64_t start_;
    uint64_t end_;
    bool started_;
};
#endif