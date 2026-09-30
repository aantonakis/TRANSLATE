#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

#include <vector>
#include <mutex>

class ProgressBar
{
private:
    int num_threads;
    std::vector<double> progress;
    std::vector<int> file_number;
    mutable std::mutex mutex;
public:
    ProgressBar(int threads);

    void update(double prog, int thread);
    void new_file(int file, int thread);
    void display() const;
    bool min_prog(int thread) const;
};

#endif
