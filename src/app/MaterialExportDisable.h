#pragma once
#include "io/MaterialExport.h"
#include <memory>

namespace lholo::app {
// Invoke before ANY teardown. Retain the native module on timeout in addition
// to the loader's false-return contract. Never drop its last external owner
// from inside DLL code. The SDK lifecycle callback must still own the module
// when a subsequent retry releases our retained reference.
template<class Owner>
bool prepareMaterialExportDisable(io::MaterialExportJob& job,
                                  std::shared_ptr<Owner> const& callbackOwner,
                                  std::shared_ptr<Owner>& retainedOwner,
                                  std::chrono::milliseconds budget) {
    if (!callbackOwner) return false;
    if (!job.closeAndDrain(budget)) {
        retainedOwner=callbackOwner;
        return false;
    }
    if (retainedOwner) {
        // callbackOwner + retainedOwner need a third, external loader owner.
        if (retainedOwner != callbackOwner || callbackOwner.use_count() <= 2) return false;
        retainedOwner.reset();
    }
    return true;
}
}
