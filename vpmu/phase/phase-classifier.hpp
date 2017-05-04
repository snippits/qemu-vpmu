#ifndef __PHASE_CLASSIFIER_HPP_
#define __PHASE_CLASSIFIER_HPP_

#include "vpmu.hpp"  // Include types and basic headers
#include "phase.hpp" // Phase class

class PhaseClassifier : public VPMULog
{
public:
    PhaseClassifier() : VPMULog("PhaseClassifier"){};

    void set_similarity_threshold(uint64_t new_threshold)
    {
        similarity_threshold = new_threshold;
    }

    virtual Phase& classify(std::vector<Phase>& phase_list, const Phase& phase)
    {
        log_fatal("classify() is not implemented");
        return Phase::not_found;
    }

    virtual Phase& classify(std::vector<Phase>& phase_list, const Window& window)
    {
        log_fatal("classify() is not implemented");
        return Phase::not_found;
    }

protected:
    uint64_t similarity_threshold = 1;
};

#endif
