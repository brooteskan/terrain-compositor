#pragma once

#include <AzCore/Debug/Trace.h>
#include <AzCore/std/parallel/thread.h>

namespace TerrainCompositor
{
    //! Control objects are constructed/used on the entity thread; export-only objects stay on their builder thread.
    class HeightmapControlThread
    {
    public:
        //! Entity activation establishes ownership after asynchronous deserialization. Call only while inactive.
        void BindForActivation() { m_thread = AZStd::this_thread::get_id(); }
        bool Check() const
        {
            const bool valid = m_thread == AZStd::this_thread::get_id();
            AZ_Assert(valid, "Heightmap control operations must run on the owning entity/main thread.");
            return valid;
        }
    private:
        AZStd::thread_id m_thread = AZStd::this_thread::get_id();
    };
} // namespace TerrainCompositor
