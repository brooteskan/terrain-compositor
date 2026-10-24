#pragma once

#include "EditorPreviewStatus.h"
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <TerrainCompositor/Components/TerrainCompositionGradientComponent.h>

namespace TerrainCompositor
{
    //! Editor-only, edit-driven delivery to retained-material publishers. No polling.
    class TerrainHeightPrecisionNotifications : public AZ::EBusTraits
    {
    public:
        virtual void OnHeightPrecisionChanged() {}
    };
    using TerrainHeightPrecisionNotificationBus = AZ::EBus<TerrainHeightPrecisionNotifications>;

    //! Read-only renderer evidence for inspector/automation acceptance tests.
    class TerrainHeightPrecisionRequests : public AZ::EBusTraits
    {
    public:
        using BusIdType = AZ::EntityId;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        virtual AZ::u32 GetRenderedHeightPrecision() const = 0;
        virtual AZ::u32 GetHeightVertexStride() const = 0;
    };
    using TerrainHeightPrecisionRequestBus = AZ::EBus<TerrainHeightPrecisionRequests>;

    class EditorTerrainCompositionGradientComponent final
        : public AzToolsFramework::Components::EditorComponentBase
        , private AzFramework::EntityDebugDisplayEventBus::Handler
        , private TerrainHeightPrecisionRequestBus::Handler
    {
    public:
        using BaseClass = AzToolsFramework::Components::EditorComponentBase;
        // Intentional reflected editor adapter; component identity and Configuration ownership must remain explicit.
        AZ_COMPONENT(EditorTerrainCompositionGradientComponent, EditorTerrainCompositionGradientComponentTypeId, BaseClass);
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services);

        void Activate() override;
        void Deactivate() override;
        void BuildGameEntity(AZ::Entity* gameEntity) override;
        AZ::TypeId GetUnderlyingComponentType() const override;
        bool ReadInConfig(const AZ::ComponentConfig* configuration) override;
        bool WriteOutConfig(AZ::ComponentConfig* configuration) const override;

    private:
        template<class> friend class TerrainEditorPreviewLifecycleTests;
        AZ::u32 OnConfigurationChanged();
        AZStd::string GetStatusText() const;
        AZ::u32 GetRenderedHeightPrecision() const override;
        AZ::u32 GetHeightVertexStride() const override;
        void DisplayEntityViewport(const AzFramework::ViewportInfo& viewportInfo,
            AzFramework::DebugDisplayRequests& debugDisplay) override;

        TerrainCompositionConfig m_configuration;
        EditorPreview<TerrainCompositionGradientComponent> m_preview{ *this, "Inactive: no composed gradient." };
    };
} // namespace TerrainCompositor
