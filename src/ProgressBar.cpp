#include <iomanip>
#include <iostream>

#include "ProgressBar.h"

/*
 * The constructor of the progress bar class. Allocates the necessary array space.
 */
ProgressBar::ProgressBar(int threads):
    num_threads(threads)
{
    for (int i = 0; i < num_threads; i++) {
        progress.push_back(0);
        file_number.push_back(i + 1);
    }

    std::cout << std::fixed << std::setprecision(3);
}

/*
 * Updates the progress bar.
 * 
 * @param prog The progress (in percent) of the associated thread
 * @param thread The thread to update
 */
void ProgressBar::update(double prog, int thread)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (prog > progress[thread])
        progress[thread] = prog;
}

/*
 * Resets a thread to display the progress of its new simulation.
 * 
 * @param file The file number associated with the new simulation
 * @param thread The thread to reset
 */
void ProgressBar::new_file(int file, int thread)
{
    std::lock_guard<std::mutex> lock(mutex);
    file_number[thread] = file;
    progress[thread] = 0;
}

/*
 * Displays the percent progress of all current simulations. 
 */
void ProgressBar::display() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << "\r";
    for (int k = 0; k < num_threads; k++)
        std::cout << "File " << file_number[k] << ": " << progress[k] * 100 << "%   ";
    std::cout << std::flush;
}

/*
 * Returns whether the given thread is the least progressed of all current simulation.
 * This is used for display purposes, as otherwise each thread's display command would compete.
 * 
 * @param thread The thread to check
 * @return Whether it is the least progressed simulation of all current ones
 */
bool ProgressBar::min_prog(int thread) const
{
    std::lock_guard<std::mutex> lock(mutex);
    for (int k = 0; k < num_threads; k++)
        if (progress[thread] > progress[k])
            return false;

    return true;
}
