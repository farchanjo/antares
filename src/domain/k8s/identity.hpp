#pragma once

#include <string>

#include "domain/k8s/uid.hpp"

namespace antares::domain::k8s {

/// What a view renders to name an object. The namespace and the kind are not
/// here: both are properties of the scope the entry is stored in, so repeating
/// them per row would cost bytes for no information (FR-003).
struct Identity {
    Uid uid;
    std::string name;
};

} // namespace antares::domain::k8s
