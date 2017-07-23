#ifndef __PHASE_DETECT_HPP_
#define __PHASE_DETECT_HPP_
#pragma once

#include "vpmu.hpp"             // Include types and basic headers
#include "vpmu-log.hpp"         // VPMULog
#include "vpmu-utils.hpp"       // Various functions
#include "vpmu-packet.hpp"      // All performance counter data types
#include "phase-classifier.hpp" // PhaseClassifier class
#include "phase-common.hpp"     // Common definitions of phase detection

class PhaseDetect
{
public:
    // No default constructor for this class
    PhaseDetect() = delete;

    PhaseDetect(uint64_t                           new_window_size,
                std::unique_ptr<PhaseClassifier>&& new_classifier)
    {
        // window size can be changed in the code
        window_size = new_window_size;
        classifier  = std::move(new_classifier);
    }

    void change_classifier(std::unique_ptr<PhaseClassifier>&& new_classifier)
    {
        classifier = std::move(new_classifier);
    }

    inline Phase& classify(std::vector<Phase>& phase_list, const Window& window)
    {
        return classifier->classify(phase_list, window);
    }

    inline Phase& classify(std::vector<Phase>& phase_list, const Phase& phase)
    {
        return classifier->classify(phase_list, phase);
    }

    void set_window_size(uint64_t new_size) { window_size = new_size; }
    inline uint64_t               get_window_size(void) { return window_size; }

    void dump_data(FILE* fp, VPMU_Insn::Data data);
    void dump_data(FILE* fp, VPMU_Cache::Data data);
    void dump_data(FILE* fp, VPMU_Branch::Data data);

private:
    uint64_t window_size;
    // C++ must use pointer in order to call the derived class virtual functions
    std::unique_ptr<PhaseClassifier> classifier;
};

extern PhaseDetect phase_detect;

#endif
