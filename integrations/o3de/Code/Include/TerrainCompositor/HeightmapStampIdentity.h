#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Math/Uuid.h>
#include <AzCore/std/containers/span.h>
#include <AzCore/std/string/string.h>
#include <AzCore/std/string/string_view.h>

namespace TerrainCompositor
{
    //! Canonical, case-sensitive, length-prefixed aliases. The root instance alias is deliberately omitted.
    AZStd::string MakePrefabStampOrderKey(AZStd::span<const AZStd::string> instanceAliases, AZStd::string_view entityAlias);
    AZStd::string MakeUuidStampOrderKey(const AZ::Uuid& id);
    void AssignNewStampOrderingIdentity(AZ::Uuid& id, AZStd::string& key);
    AZStd::string GetRuntimeStampOrderKey(const AZ::Uuid& id, const AZStd::string& key);
    bool IsValidStampOrderKey(AZStd::string_view key);
    bool StampPriorityLess(AZ::s32 leftPriority, AZStd::string_view leftKey, AZ::s32 rightPriority, AZStd::string_view rightKey);
    bool StampOrderKeyLess(AZStd::string_view left, AZStd::string_view right);

    //! Implemented only by Tools/Builders. Runtime stamps never consult this interface.
    class HeightmapStampIdentityRequests
    {
    public:
        AZ_RTTI(HeightmapStampIdentityRequests, "{4178CFE3-DCE8-4510-A8AB-657E8865A990}");
        virtual ~HeightmapStampIdentityRequests() = default;
        //! Empty means unresolved/propagating; callers must not reuse a copied prefab key.
        virtual AZStd::string ResolveStampOrderKey(AZ::EntityId editorEntityId) const = 0;
    };
    using HeightmapStampIdentityInterface = AZ::Interface<HeightmapStampIdentityRequests>;

    //! Main-thread editor notifications. No runtime registration map is maintained by the resolver.
    class HeightmapStampIdentityNotifications : public AZ::EBusTraits
    {
    public:
        virtual ~HeightmapStampIdentityNotifications() = default;
        virtual void OnStampIdentitiesChanged() = 0;
    };
    using HeightmapStampIdentityNotificationBus = AZ::EBus<HeightmapStampIdentityNotifications>;
} // namespace TerrainCompositor
