#include <TerrainCompositor/HeightmapDataCache.h>
#include <TerrainCompositor/Internal/CacheLifecycle.h>
#include "AssetSource.h"

#include <Atom/RPI.Public/RPIUtils.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/limits.h>
#include <AzCore/std/smart_ptr/enable_shared_from_this.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Asset/AssetCatalogBus.h>
#include <algorithm>
#include <cstring>

namespace TerrainCompositor
{
    namespace
    {
        // Main-thread publication sequence, unique even if a weak cache entry is evicted while old query data survives.
        AZ::u64 s_nextRevision = 0;

        constexpr const char* ImportGuidance =
            "Use unsigned 16-bit grayscale TIFF with GSI16: Linear source/destination, uncompressed R16/R16_UNORM, "
            "power-of-two dimensions, resolution reduction 0, and full image-tag quality (mip 0). "
            "Reprocess the image and check Asset Processor errors and its streamingimage/mip-chain products.";


        bool IsPowerOfTwo(AZ::u32 value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }
    } // namespace

    //! Asset notifications may originate on other threads (and BusConnect can deliver them inline).
    //! Watchers only enqueue owned event payloads. All state changes, decoding, and subscriber calls run on SystemTick.
    class HeightmapDataSource final
        : public AZStd::enable_shared_from_this<HeightmapDataSource>
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        explicit HeightmapDataSource(AZ::Data::AssetId id)
            : m_assetId(id)
        {
        }

        ~HeightmapDataSource()
        {
            Stop();
        }

        void Start()
        {
            if (!m_controlThread.Check()) { return; }
            m_active = true;
            // Set once before connecting; catalog callbacks only read this weak pointer and the constant asset ID.
            m_weakSelf = shared_from_this();
            AzFramework::AssetCatalogEventBus::Handler::BusConnect();
            StartImage();
        }

        void Stop()
        {
            if (!m_controlThread.Check()) { return; }
            m_active = false;
            ++m_imageGeneration;
            ++m_mipGeneration;
            AzFramework::AssetCatalogEventBus::Handler::BusDisconnect();
            m_imageWatcher.reset();
            m_mipWatcher.reset();
            m_image.Reset();
            m_mip.Reset();
            Publish(HeightmapDataStatus::Unassigned);
            m_changed.DisconnectAllHandlers();
        }

        HeightmapControlThread m_controlThread;
        HeightmapDataSnapshot m_snapshot;
        HeightmapDataCache::ChangedEvent m_changed;

    private:
        enum class AssetEvent { Ready, Reloaded, PreReload, Error, ReloadError, Removed };

        class AssetWatcher final : private AZ::Data::AssetBus::Handler
        {
        public:
            AssetWatcher(AZStd::weak_ptr<HeightmapDataSource> owner, AZ::u64 generation, bool image)
                : m_owner(AZStd::move(owner)), m_generation(generation), m_image(image)
            {
            }
            ~AssetWatcher() { BusDisconnect(); }
            void Connect(AZ::Data::AssetId id) { BusConnect(id); }

        private:
            void Queue(AssetEvent event, AZ::Data::Asset<AZ::Data::AssetData> asset = {})
            {
                // Capture the token now: O3DE can change the old AssetData's token when replacing it on reload.
                const int token = asset.Get() ? asset->GetCreationToken() : -1;
                AZ::SystemTickBus::QueueFunction(
                    [owner = m_owner, generation = m_generation, image = m_image, event, asset, token]()
                    {
                        if (auto source = owner.lock(); source && source->m_active)
                        {
                            source->OnAssetEvent(generation, image, event, asset, token);
                        }
                    });
            }
            void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override { Queue(AssetEvent::Ready, asset); }
            void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override { Queue(AssetEvent::Reloaded, asset); }
            void OnAssetPreReload(AZ::Data::Asset<AZ::Data::AssetData> asset) override { Queue(AssetEvent::PreReload, asset); }
            void OnAssetError(AZ::Data::Asset<AZ::Data::AssetData> asset) override { Queue(AssetEvent::Error, asset); }
            void OnAssetReloadError(AZ::Data::Asset<AZ::Data::AssetData> asset) override { Queue(AssetEvent::ReloadError, asset); }
            void OnAssetCanceled([[maybe_unused]] AZ::Data::AssetId id) override { Queue(AssetEvent::Removed); }
            // Do not treat OnAssetUnloaded as product removal. AssetManager queues it by ID after destroying
            // an old instance, so a new watcher can receive it after editor/Play reactivation. This source
            // retains strong image/mip handles: its current instances cannot unload while subscribed.
            // Subscription generations cannot reject an event delivered to the new watcher in the first place.
            // Actual product removal is handled separately by OnCatalogAssetRemoved.

            const AZStd::weak_ptr<HeightmapDataSource> m_owner;
            const AZ::u64 m_generation;
            const bool m_image;
        };

        void Publish(HeightmapDataStatus status, const char* reason = nullptr, HeightmapDataPtr data = {})
        {
            if (m_snapshot.m_status == status && m_snapshot.m_data == data)
            {
                return;
            }
            const AZ::u64 revision = data ? data->m_revision : ++s_nextRevision;
            m_snapshot = { status, revision, AZStd::move(data), m_assetId };
            if (reason)
            {
                AZ_Warning("HeightmapData", false, "Heightmap '%s' (%s): %s %s", m_assetPath.c_str(),
                    m_assetId.ToFixedString().c_str(), reason, ImportGuidance);
            }
            // Subscribers can disconnect or switch assets from a change handler. The queued task owns this source until return.
            const auto snapshot = m_snapshot;
            m_changed.Signal(snapshot);
        }

        void ClearMip()
        {
            ++m_mipGeneration;
            m_mipWatcher.reset();
            m_mip.Reset();
            m_waitingForMipReload = false;
            m_reloadMipAfterReady = false;
            m_lastMipToken = -1;
        }

        void StartImage()
        {
            ++m_imageGeneration;
            m_imageWatcher.reset();
            ClearMip();
            m_image.Reset();
            m_waitingForImageReload = false;
            m_lastImageToken = -1;

            const auto info = Internal::GetAssetInfo(m_assetId);
            m_assetPath = info.m_relativePath;
            // Check the catalog before GetAsset: StreamingImageAsset's missing-asset fallback is a color image,
            // and its handler can even synchronously compile a fallback. It must never become terrain height data.
            if (!info.m_assetId.IsValid())
            {
                Publish(HeightmapDataStatus::Missing, "Image product is missing from the asset catalog.");
                return;
            }
            if (info.m_assetType != azrtti_typeid<AZ::RPI::StreamingImageAsset>())
            {
                Publish(HeightmapDataStatus::Unsupported, "Select the StreamingImage product, not a source file or another product type.");
                return;
            }
            Publish(HeightmapDataStatus::Loading);
            m_image = AZ::Data::AssetManager::Instance().GetAsset<AZ::RPI::StreamingImageAsset>(
                m_assetId, AZ::Data::AssetLoadBehavior::QueueLoad);
            m_imageWatcher = AZStd::make_unique<AssetWatcher>(m_weakSelf, m_imageGeneration, true);
            // Queue first, connect second: the connection policy also reports assets which are already ready/error.
            m_imageWatcher->Connect(m_assetId);
            if (!m_image.Get())
            {
                Publish(HeightmapDataStatus::Error, "Could not queue the image product.");
            }
        }

        void PrepareImage(bool reloaded)
        {
            ClearMip();
            Publish(HeightmapDataStatus::Loading);
            const auto& descriptor = m_image->GetImageDescriptor();
            if (descriptor.m_format != AZ::RHI::Format::R16_UNORM)
            {
                Publish(HeightmapDataStatus::Unsupported, "Product format is not R16_UNORM; low-precision, color, and compressed formats are unsupported.");
                return;
            }
            if (descriptor.m_dimension != AZ::RHI::ImageDimension::Image2D || descriptor.m_arraySize != 1 ||
                descriptor.m_size.m_depth != 1 || !IsPowerOfTwo(descriptor.m_size.m_width) ||
                !IsPowerOfTwo(descriptor.m_size.m_height) || descriptor.m_mipLevels == 0 || m_image->GetMipChainCount() == 0)
            {
                Publish(HeightmapDataStatus::Unsupported, "Expected a nonempty, power-of-two 2D image with one slice and mip 0.");
                return;
            }
            const size_t chainIndex = m_image->GetMipChainIndex(0);
            if (chainIndex >= m_image->GetMipChainCount() || m_image->GetMipLevel(chainIndex) != 0 ||
                m_image->GetMipCount(chainIndex) == 0)
            {
                Publish(HeightmapDataStatus::Unsupported, "Product does not contain a valid mip-zero chain.");
                return;
            }
            if (chainIndex == m_image->GetMipChainCount() - 1)
            {
                // The embedded tail is initialized by the image handler before image readiness.
                Decode(m_image->GetTailMipChain());
                return;
            }

            const auto& reference = m_image->GetMipChainAsset(chainIndex);
            const auto info = Internal::GetAssetInfo(reference.GetId());
            if (!info.m_assetId.IsValid())
            {
                Publish(HeightmapDataStatus::Missing, "Required mip-zero product is missing from the asset catalog.");
                return;
            }
            if (info.m_assetType != azrtti_typeid<AZ::RPI::ImageMipChainAsset>())
            {
                Publish(HeightmapDataStatus::Unsupported, "Required mip-zero dependency has the wrong product type.");
                return;
            }
            m_mip = AZ::Data::AssetManager::Instance().FindOrCreateAsset<AZ::RPI::ImageMipChainAsset>(
                reference.GetId(), AZ::Data::AssetLoadBehavior::NoLoad);
            // An in-progress load can also belong to an older parent. Finish it asynchronously, then refresh it
            // before decoding; Reload() cannot start while the asset's first load is still in flight.
            m_reloadMipAfterReady = !m_mip.IsReady() && (reloaded || m_mip.IsLoading());
            m_mip.QueueLoad();
            m_mipWatcher = AZStd::make_unique<AssetWatcher>(m_weakSelf, m_mipGeneration, false);
            // A cached mip may belong to the previous parent revision. ImageMipChainAsset disables automatic reload,
            // and StreamingImageAsset only refreshes dependencies itself in some load configurations. Explicitly
            // refresh any resident dependency not already owned by this ready parent (and every parent reload).
            m_waitingForMipReload = m_mip.IsReady() && (reloaded || !reference.IsReady() || reference.Get() != m_mip.Get());
            m_mipWatcher->Connect(reference.GetId());
            if (m_waitingForMipReload)
            {
                m_reloadMipAfterReady = false;
                m_mip.Reload();
            }
            if (!m_mip.Get())
            {
                Publish(HeightmapDataStatus::Error, "Could not queue the required mip-zero product.");
            }
        }

        void Decode(const AZ::RPI::ImageMipChainAsset& chain)
        {
            if (chain.GetMipLevelCount() == 0 || chain.GetArraySize() != 1 || chain.GetSubImageCount() == 0)
            {
                Publish(HeightmapDataStatus::Unsupported, "Mip-zero dependency has invalid topology.");
                return;
            }
            const auto& descriptor = m_image->GetImageDescriptor();
            const auto& layout = chain.GetSubImageLayout(0);
            // This is ImageMipChainAsset's non-loading accessor, never StreamingImageAsset::GetSubImageData.
            const auto bytes = chain.GetSubImageData(0, 0);
            const size_t width = descriptor.m_size.m_width;
            const size_t height = descriptor.m_size.m_height;
            const size_t rowBytes = width * sizeof(AZ::u16);
            const AZ::u64 requiredBytes = AZ::u64(layout.m_bytesPerRow) * (height - 1) + rowBytes;
            if (!bytes.data() || layout.m_size != descriptor.m_size || layout.m_rowCount != height || layout.m_bytesPerRow < rowBytes ||
                requiredBytes > bytes.size() || requiredBytes > layout.m_bytesPerImage ||
                height > AZStd::numeric_limits<size_t>::max() / width / sizeof(float))
            {
                Publish(HeightmapDataStatus::Unsupported, "Mip-zero dimensions, row layout, or pixel byte count do not match the image descriptor.");
                return;
            }

            auto data = AZStd::make_shared<HeightmapData>();
            data->m_assetId = m_assetId;
            data->m_width = descriptor.m_size.m_width;
            data->m_height = descriptor.m_size.m_height;
            data->m_samples.resize(width * height);
            data->m_rawSamples.resize(width * height);
            auto rowDescriptor = descriptor;
            rowDescriptor.m_size.m_height = 1;
            // RPI's pixel helper expects tightly packed rows. Present one validated row at a time so padding is honored.
            // The aligned copy also avoids reinterpreting a possibly unaligned byte buffer as uint16_t in the helper.
            AZStd::vector<AZ::u16> row(width);
            for (AZ::u32 y = 0; y < data->m_height; ++y)
            {
                std::memcpy(row.data(), bytes.data() + size_t(y) * layout.m_bytesPerRow, rowBytes);
                std::memcpy(data->m_rawSamples.data() + size_t(y) * width, row.data(), rowBytes);
                const AZStd::span<const AZ::u8> rowData(reinterpret_cast<const AZ::u8*>(row.data()), rowBytes);
                for (AZ::u32 x = 0; x < data->m_width; ++x)
                {
                    data->m_samples[size_t(y) * width + x] =
                        AZ::RPI::GetImageDataPixelValue<float>(rowData, rowDescriptor, x, 0, 0);
                }
            }
            data->m_uniqueNonzeroValues.reserve(data->m_rawSamples.size());
            for (const AZ::u16 value : data->m_rawSamples)
            {
                if (value != 0)
                {
                    data->m_uniqueNonzeroValues.push_back(value);
                }
            }
            AZStd::sort(data->m_uniqueNonzeroValues.begin(), data->m_uniqueNonzeroValues.end());
            data->m_uniqueNonzeroValues.erase(
                AZStd::unique(data->m_uniqueNonzeroValues.begin(), data->m_uniqueNonzeroValues.end()),
                data->m_uniqueNonzeroValues.end());
            data->m_revision = ++s_nextRevision;
            Publish(HeightmapDataStatus::Ready, nullptr, AZStd::move(data));
        }

        void OnAssetEvent(AZ::u64 generation, bool image, AssetEvent event,
            const AZ::Data::Asset<AZ::Data::AssetData>& asset, int token)
        {
            if (!m_controlThread.Check() || !m_active) { return; }
            if (generation != (image ? m_imageGeneration : m_mipGeneration))
            {
                return; // Old subscription/parent revision. In particular, a removed image cannot be resurrected.
            }
            const auto expectedId = image ? m_assetId : m_mip.GetId();
            if (asset.GetId().IsValid() && asset.GetId() != expectedId)
            {
                return; // Never accept an engine fallback image or an unrelated dependency.
            }
            if (event == AssetEvent::PreReload)
            {
                if (token < (image ? m_lastImageToken : m_lastMipToken))
                {
                    return;
                }
                if (image)
                {
                    m_waitingForImageReload = true;
                    ClearMip();
                    Publish(HeightmapDataStatus::Loading);
                }
                // Mip pre-reload can be emitted even when O3DE refuses automatic reload. Our explicit reload
                // and catalog-change paths already invalidate data; do not wait for a nonexistent auto-reload.
                return;
            }
            if (event == AssetEvent::Removed)
            {
                InvalidateRemoved(image);
                return;
            }
            if (event == AssetEvent::Error || event == AssetEvent::ReloadError)
            {
                if (event == AssetEvent::ReloadError && image && !m_waitingForImageReload)
                {
                    return; // A failed older reload cannot clear a newer accepted revision.
                }
                if (event == AssetEvent::Error && asset.Get() &&
                    asset.Get() != (image ? m_image.GetData() : m_mip.GetData()))
                {
                    return;
                }
                if (image)
                {
                    ClearMip();
                    m_waitingForImageReload = true;
                }
                else
                {
                    m_waitingForMipReload = true;
                }
                // Do not leave last-known-good heights active on failure. Existing queries may finish with their copies.
                Publish(HeightmapDataStatus::Error, image ? "Image load/reload failed." : "Mip-zero load/reload failed.");
                return;
            }
            int& lastToken = image ? m_lastImageToken : m_lastMipToken;
            bool& waitingForReload = image ? m_waitingForImageReload : m_waitingForMipReload;
            if (!asset.IsReady() || token < 0 || asset->GetCreationToken() != token ||
                (waitingForReload && event != AssetEvent::Reloaded) ||
                (lastToken >= 0 && token <= lastToken))
            {
                return; // Duplicate ready replay or stale revision; decoding is once per accepted revision.
            }
            if (event == AssetEvent::Ready && asset.Get() != (image ? m_image.GetData() : m_mip.GetData()))
            {
                return; // A queued ready from a superseded load is not the load this subscription requested.
            }
            lastToken = token;
            waitingForReload = false;
            if (image)
            {
                m_image = asset;
                PrepareImage(event == AssetEvent::Reloaded);
            }
            else
            {
                m_mip = asset;
                if (m_reloadMipAfterReady)
                {
                    m_reloadMipAfterReady = false;
                    m_waitingForMipReload = true;
                    m_mip.Reload();
                    return;
                }
                Decode(*m_mip);
            }
        }

        void InvalidateRemoved(bool image)
        {
            if (image)
            {
                ++m_imageGeneration;
                m_imageWatcher.reset();
                m_image.Reset();
            }
            // Keep the parent subscription on mip removal: a parent reload may already be in flight and may
            // remove that dependency entirely (for example, when the image shrinks to an embedded mip-zero tail).
            ClearMip();
            Publish(HeightmapDataStatus::Missing, "Image or required mip-zero product was removed or canceled.");
        }

        enum class CatalogEvent { Available, Removed, Loaded };
        void QueueCatalogEvent(CatalogEvent event, AZ::Data::AssetId id = {})
        {
            // No component pointers cross threads. Filter against the current dependency only on the main thread.
            AZ::SystemTickBus::QueueFunction([owner = m_weakSelf, event, id]()
            {
                if (auto source = owner.lock(); source && source->m_active)
                {
                    source->OnCatalogEvent(event, id);
                }
            });
        }

        void OnCatalogEvent(CatalogEvent event, AZ::Data::AssetId id)
        {
            if (!m_controlThread.Check() || !m_active) { return; }
            AZ::Data::AssetId mipId = m_mip.GetId();
            if (m_image.IsReady() && m_image->GetMipChainCount() > 1)
            {
                mipId = m_image->GetMipChainAsset(0).GetId();
            }
            if (event != CatalogEvent::Loaded && id != m_assetId && id != mipId)
            {
                // While missing, a mip can reappear after its parent was invalidated and released.
                if (!(m_snapshot.m_status == HeightmapDataStatus::Missing && id.m_guid == m_assetId.m_guid))
                {
                    return;
                }
            }
            if (event == CatalogEvent::Removed)
            {
                // Catalog callbacks are queued without touching mutable load state on asset threads.
                // An older removal cannot discard a product which has since reappeared in the catalog.
                if (Internal::GetAssetInfo(id).m_assetId.IsValid()) { return; }
                InvalidateRemoved(id == m_assetId);
            }
            else if (event == CatalogEvent::Available && !Internal::GetAssetInfo(id).m_assetId.IsValid())
            {
                return; // A stale availability event cannot restart a product already removed again.
            }
            else if (m_snapshot.m_status == HeightmapDataStatus::Missing || m_snapshot.m_status == HeightmapDataStatus::Error ||
                (m_snapshot.m_status == HeightmapDataStatus::Unsupported && !m_image.Get()))
            {
                StartImage();
                // AssetManager may still own a ready/error product after removal or a failed reload. Request a fresh revision.
                if (m_image.Get() && !m_image.IsLoading())
                {
                    m_waitingForImageReload = true;
                    m_image.Reload();
                }
            }
            else if (event == CatalogEvent::Available && id == mipId && m_mip.Get() && !m_waitingForImageReload)
            {
                // Standalone mip products opt out of automatic reload. Refresh explicitly, including when the parent is unchanged.
                Publish(HeightmapDataStatus::Loading);
                if (m_mip.IsLoading())
                {
                    m_reloadMipAfterReady = true;
                }
                else
                {
                    m_reloadMipAfterReady = false;
                    m_waitingForMipReload = true;
                    m_mip.Reload();
                }
            }
            // Parent revisions arrive via AssetBus. Do not accept the old ready parent just because the catalog changed.
        }

        void OnCatalogLoaded([[maybe_unused]] const char* catalogFile) override { QueueCatalogEvent(CatalogEvent::Loaded); }
        void OnCatalogAssetAdded(const AZ::Data::AssetId& id) override { QueueCatalogEvent(CatalogEvent::Available, id); }
        void OnCatalogAssetChanged(const AZ::Data::AssetId& id) override { QueueCatalogEvent(CatalogEvent::Available, id); }
        void OnCatalogAssetRemoved(const AZ::Data::AssetId& id, [[maybe_unused]] const AZ::Data::AssetInfo& info) override
        {
            QueueCatalogEvent(CatalogEvent::Removed, id);
        }

        const AZ::Data::AssetId m_assetId;
        AZStd::string m_assetPath;
        AZStd::weak_ptr<HeightmapDataSource> m_weakSelf;
        AZ::Data::Asset<AZ::RPI::StreamingImageAsset> m_image;
        AZ::Data::Asset<AZ::RPI::ImageMipChainAsset> m_mip;
        AZStd::unique_ptr<AssetWatcher> m_imageWatcher;
        AZStd::unique_ptr<AssetWatcher> m_mipWatcher;
        AZ::u64 m_imageGeneration = 0;
        AZ::u64 m_mipGeneration = 0;
        int m_lastImageToken = -1;
        int m_lastMipToken = -1;
        bool m_waitingForImageReload = false;
        bool m_waitingForMipReload = false;
        bool m_reloadMipAfterReady = false;
        bool m_active = false;
    };

    HeightmapDataCache::HeightmapDataCache()
    {
        HeightmapDataCacheInterface::Register(this);
        Internal::CacheLifecycle<HeightmapDataCache>::Signal(true);
    }

    HeightmapDataCache::~HeightmapDataCache()
    {
        HeightmapDataCacheInterface::Unregister(this);
        Internal::CacheLifecycle<HeightmapDataCache>::Signal(false);
        Internal::StopAssetSources(m_sources);
    }

    HeightmapDataCache::Handle HeightmapDataCache::Acquire(const AZ::Data::AssetId& assetId)
    {
        if (!m_controlThread.Check()) { return {}; }
        if (!assetId.IsValid())
        {
            return {};
        }
        return Internal::AcquireAssetSource<HeightmapDataSource>(m_sources, assetId);
    }

    HeightmapDataSnapshot HeightmapDataCache::GetSnapshot(const Handle& handle)
    {
        return Internal::GetAssetSourceSnapshot<HeightmapDataSnapshot>(handle);
    }

    void HeightmapDataCache::ConnectChangedHandler(const Handle& handle, ChangedEvent::Handler& handler)
    {
        Internal::ConnectAssetSourceChanged(handle, handler);
    }
} // namespace TerrainCompositor
