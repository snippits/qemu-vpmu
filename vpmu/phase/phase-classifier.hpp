#ifndef __PHASE_CLASSIFIER_HPP_
#define __PHASE_CLASSIFIER_HPP_

extern "C" {
#include "vpmu-common.h" // Include types and basic headers
}
#include "phase.hpp"

class NearestCluster : public Classifier
{
public:
    Phase &classify(std::vector<Phase> &phase_list, const Phase &phase) override
    {
        double min_d = similarity_threshold;
        int    idx   = -1;
        uint64_t phase_num = (&phase - &phase_list[0]);

        std::vector<double> n_vector(phase.get_vector());
        vpmu::math::normalize(n_vector);

        for (int i = 0; i < phase_list.size(); i++) {
            // Exclude self comparison
            if (i == phase_num) continue;
            auto &phase_n_vector = phase_list[i].get_normalized_vector();
            // Calaulate the distance between a phase and the window
            double d = manhatten_distance(phase_n_vector, n_vector);
            if (d < min_d) {
                min_d = d;
                idx   = i;
            }
        }

        if (idx == -1) return Phase::not_found;
        return phase_list[idx];
    }


    Phase &classify(std::vector<Phase> &phase_list, const Window &window) override
    {
        double min_d = similarity_threshold;
        int    idx   = -1;

        std::vector<double> n_vector(window.branch_vector);
        vpmu::math::normalize(n_vector);

        for (int i = 0; i < phase_list.size(); i++) {
            auto &phase_n_vector = phase_list[i].get_normalized_vector();
            // Calaulate the distance between a phase and the window
            double d = manhatten_distance(phase_n_vector, n_vector);
            if (d < min_d) {
                min_d = d;
                idx   = i;
            }
        }

        if (idx == -1) return Phase::not_found;
        return phase_list[idx];
    }

private:
    double manhatten_distance(const std::vector<double> &v1,
                              const std::vector<double> &v2)
    {
        double m_distance = 0.0f;
        for (int i = 0; i < v1.size(); i++) {
            double abs_difference;
            if (v1[i] >= v2[i]) {
                abs_difference = v1[i] - v2[i];
            } else {
                abs_difference = v2[i] - v1[i];
            }
            m_distance += abs_difference;
        }
        return m_distance;
    }
};

#endif
