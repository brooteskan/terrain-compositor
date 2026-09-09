#include <TerrainCompositor/HeightmapStampIdentity.h>
#include <AzCore/std/algorithm.h>

namespace TerrainCompositor
{
    AZStd::string MakePrefabStampOrderKey(AZStd::span<const AZStd::string> instanceAliases, AZStd::string_view entityAlias)
    {
        if (entityAlias.empty())
        {
            return {};
        }
        AZStd::string key = "prefab-v1:";
        const auto append = [&key](char kind, AZStd::string_view alias)
        {
            key += kind;
            key += AZStd::string::format("%zu:", alias.size());
            key.append(alias.data(), alias.size());
        };
        for (const auto& alias : instanceAliases)
        {
            if (alias.empty())
            {
                return {};
            }
            append('i', alias);
        }
        append('e', entityAlias);
        return key;
    }

    AZStd::string MakeUuidStampOrderKey(const AZ::Uuid& id)
    {
        return id.IsNull() ? AZStd::string{} : "uuid-v1:" + id.ToString<AZStd::string>(false, false);
    }

    bool IsValidStampOrderKey(AZStd::string_view key)
    {
        constexpr AZStd::string_view uuidPrefix = "uuid-v1:";
        if (key.starts_with(uuidPrefix))
        {
            const auto value = key.substr(uuidPrefix.size());
            if (value.size() != 32)
            {
                return false;
            }
            const auto id = AZ::Uuid::CreateString(value.data(), value.size());
            return !id.IsNull() && MakeUuidStampOrderKey(id) == key;
        }
        constexpr AZStd::string_view prefabPrefix = "prefab-v1:";
        if (!key.starts_with(prefabPrefix))
        {
            return false;
        }
        key.remove_prefix(prefabPrefix.size());
        while (!key.empty())
        {
            const char kind = key.front();
            key.remove_prefix(1);
            if ((kind != 'i' && kind != 'e') || key.empty() || key.front() < '1' || key.front() > '9')
            {
                return false;
            }
            size_t length = 0;
            while (!key.empty() && key.front() >= '0' && key.front() <= '9')
            {
                // Bound the length before multiplication; malformed input cannot overflow size_t.
                if (length > key.size() / 10)
                {
                    return false;
                }
                length = length * 10 + static_cast<size_t>(key.front() - '0');
                key.remove_prefix(1);
            }
            if (key.empty() || key.front() != ':')
            {
                return false;
            }
            key.remove_prefix(1);
            if (length > key.size())
            {
                return false;
            }
            key.remove_prefix(length);
            if (kind == 'e')
            {
                return key.empty();
            }
        }
        return false;
    }

    bool StampOrderKeyLess(AZStd::string_view left, AZStd::string_view right)
    {
        return AZStd::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b)
        {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
    }
} // namespace TerrainCompositor
