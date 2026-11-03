/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "Modules/ModuleManager.h"
#include "NativeCacheTask.h"

class FBlueprintSourceAnalysisModule : public IModuleInterface
{
public:
    virtual void ShutdownModule() override
    {
        FNativeCacheTask::Cancel();
    }
};

IMPLEMENT_MODULE(FBlueprintSourceAnalysisModule, BlueprintSourceAnalysis)
