#include <AzTest/AzTest.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/UnitTest/UnitTest.h>

namespace TerrainCompositor
{
    class TestEnvironment final
        : public UnitTest::TraceBusHook
    {
    public:
        void SetupEnvironment() override
        {
            UnitTest::TraceBusHook::SetupEnvironment();
        }

        void TeardownEnvironment() override
        {
            AZ::GetGlobalSerializeContextModule().Cleanup();
            UnitTest::TraceBusHook::TeardownEnvironment();
        }
    };
} // namespace TerrainCompositor

AZ_UNIT_TEST_HOOK(new TerrainCompositor::TestEnvironment());
