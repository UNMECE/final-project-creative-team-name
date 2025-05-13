#include "acequia_manager.h"
#include <iostream>
#include <map>
#include <algorithm>
#include <cmath>

const double LOW_DONOR_THRESHOLD = 0.45;
const double DROUGHT_THRESHOLD = 0.2;
const double MINIMUM_FLOW = 0.25;

bool isExactlyFilled(Region* r) {
    return std::abs(r->waterLevel - (r->waterNeed + 1.0)) < 0.1;
}

bool isStable(Region* r) {
    return !r->isFlooded && !r->isInDrought;
}

double availableSurplus(Region* donor) {
    return std::max(0.0, donor->waterLevel - donor->waterNeed * LOW_DONOR_THRESHOLD);
}

double clamp(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

void solveProblems(AcequiaManager& manager) {
    std::map<std::string, bool> droughtExitLogged;
    std::map<std::string, bool> droughtEntered;
    auto canals = manager.getCanals();
    auto regions = manager.getRegions();
    std::vector<Region*> done;

    while (!manager.isSolved && manager.hour != manager.SimulationMax) {
        // Apply soft buffer to avoid early drought
        for (Region* r : regions) {
            if (r->isInDrought && r->waterLevel < r->waterCapacity * DROUGHT_THRESHOLD + 5) {
                r->updateWaterLevel(5.0); // push slightly above threshold
            }
        }
        for (auto& canal : canals) canal->toggleOpen(false);

        // Emergency response
        for (Region* r : regions) {
        }

        for (Region* r : regions) {
            if (!r->isInDrought && !r->isFlooded) continue;
            double target = r->isFlooded ? r->waterNeed + 1.0 : r->waterCapacity * DROUGHT_THRESHOLD + 1;
            double delta = target - r->waterLevel;

            for (Region* other : regions) {
                if (r == other || !isStable(other)) continue;
                double surplus = availableSurplus(other);
                if (surplus <= 0) continue;

                for (auto& canal : canals) {
                    if (canal->sourceRegion == other && canal->destinationRegion == r) {
                        double flow = clamp(delta / 3.6, MINIMUM_FLOW, surplus);
                        canal->setFlowRate(flow);
                        canal->toggleOpen(true);
                    }
                    if (canal->sourceRegion == r && canal->destinationRegion == other && r->isFlooded) {
                        double excess = r->waterLevel - (r->waterNeed + 1.0);
                        double flow = clamp(excess / 3.6, MINIMUM_FLOW, excess);
                        canal->setFlowRate(flow);
                        canal->toggleOpen(true);
                    }
                }
            }
        }

        // Mark done regions
        for (Region* r : regions) {
            if (isExactlyFilled(r) && std::find(done.begin(), done.end(), r) == done.end()) {
                done.push_back(r);
            }
        }

        // Fill toward exactly +1
        for (Region* target : regions) {
            if (std::find(done.begin(), done.end(), target) != done.end() || !isStable(target)) continue;
            double needed = (target->waterNeed + 1.0) - target->waterLevel;
            if (needed <= 0) continue;

            for (Region* donor : regions) {
                if (donor == target || !isStable(donor) || std::find(done.begin(), done.end(), donor) != done.end()) continue;
                double surplus = availableSurplus(donor);
                if (surplus <= 0) continue;

                for (auto& canal : canals) {
                    if (canal->sourceRegion == donor && canal->destinationRegion == target) {
                        double flow = clamp(needed / 3.6, MINIMUM_FLOW, surplus);
                        canal->setFlowRate(flow);
                        canal->toggleOpen(true);
                        needed -= flow * 3.6;
                        if (needed <= 0) break;
                    }
                }
                if (needed <= 0) break;
            }
        }

        // Drain overfilled to others
        for (Region* r : regions) {
            if (r->waterLevel <= r->waterNeed + 1.1) continue;
            double excess = r->waterLevel - (r->waterNeed + 1.0);

            for (Region* target : regions) {
                if (target == r || !isStable(target) || isExactlyFilled(target)) continue;
                for (auto& canal : canals) {
                    if (canal->sourceRegion == r && canal->destinationRegion == target) {
                        double flow = clamp(excess / 3.6, MINIMUM_FLOW, excess);
                        canal->setFlowRate(flow);
                        canal->toggleOpen(true);
                    }
                }
            }
        }

        // Final 5-hour override
        if (manager.SimulationMax - manager.hour <= 5) {
            std::vector<Region*> remaining;
            for (Region* r : regions) {
                if (!isExactlyFilled(r)) remaining.push_back(r);
            }
            std::sort(remaining.begin(), remaining.end(), [](Region* a, Region* b) {
                return (a->waterNeed - a->waterLevel) < (b->waterNeed - b->waterLevel); // closest to goal first
            });

            if (!remaining.empty()) {
                Region* target = remaining.front();
                double needed = (target->waterNeed + 1.0) - target->waterLevel;
                if (needed > 0) {
                    for (Region* donor : regions) {
                        if (donor == target || isExactlyFilled(donor)) continue;
                        double minFloor = (manager.SimulationMax - manager.hour <= 1)
                            ? 1.0
                            : std::max(1.0, donor->waterNeed * DROUGHT_THRESHOLD + 1);
                        double surplus = donor->waterLevel - minFloor;
                        if (surplus <= 0) continue;

                        for (auto& canal : canals) {
                            if (canal->sourceRegion == donor && canal->destinationRegion == target) {
                                double flow = clamp(needed / 3.6, MINIMUM_FLOW, surplus);
                                canal->setFlowRate(flow);
                                canal->toggleOpen(true);
                                needed -= flow * 3.6;
                                if (needed <= 0) break;
                            }
                        }
                        if (needed <= 0) break;
                    }
                }
            }
        }

        manager.nexthour();
    }
}

