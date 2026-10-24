#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>

namespace TerrainCompositor::GraphClipmaps
{
    constexpr int CacheSize = 512;
    struct Cell
    {
        int64_t x{}, y{};
        bool operator==(const Cell&) const = default;
    };
    struct Region
    {
        int64_t x{}, y{};
        int width{}, height{};
        uint64_t Area() const { return uint64_t(width) * height; }
    };
    inline int64_t FloorDivide(int64_t a, int b)
    {
        const auto q = a / b, r = a % b;
        return q - (r < 0);
    }
    inline int Wrap(int64_t a) { return int(a - FloorDivide(a, CacheSize) * CacheSize); }
    inline int TileIndex(Cell tile) { return int((uint64_t(tile.x) & 1) | ((uint64_t(tile.y) & 1) << 1)); }
    inline Region Intersect(Region a, Region b)
    {
        const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
        const auto right = std::min(a.x + a.width, b.x + b.width);
        const auto top = std::min(a.y + a.height, b.y + b.height);
        return {x, y, int(std::max(int64_t(0), right-x)), int(std::max(int64_t(0), top-y))};
    }

    // CPU bookkeeping only. GPU completion is ordered by the graphics frame graph.
    // Complete is called after the pass has been submitted, before planning again.
    class Schedule
    {
    public:
        void Plan(Cell origin, bool invalidateContent, bool refillCoverage, uint32_t& nextTag)
        {
            const auto dx = origin.x-m_origin.x, dy = origin.y-m_origin.y;
            const bool teleport = !m_valid || dx >= CacheSize || dx <= -CacheSize || dy >= CacheSize || dy <= -CacheSize;
            const bool full = teleport || invalidateContent || refillCoverage || m_pending.size() > 64;
            const bool newTags = teleport || invalidateContent || m_pending.size() > 64;
            const Region window{origin.x,origin.y,CacheSize,CacheSize};
            if(full) { m_pending.clear(); m_pending.push_back(window); }
            else if(dx || dy)
            {
                std::deque<Region> retained;
                for(auto r:m_pending) { r=Intersect(r,window); if(r.Area()) retained.push_back(r); }
                m_pending.swap(retained);
                if(dx>0) m_pending.push_back({origin.x+CacheSize-dx,origin.y,int(dx),CacheSize});
                if(dx<0) m_pending.push_back({origin.x,origin.y,int(-dx),CacheSize});
                const auto x=origin.x+std::max(int64_t(0),-dx);
                const int width=CacheSize-int(dx<0?-dx:dx);
                if(dy>0) m_pending.push_back({x,origin.y+CacheSize-dy,width,int(dy)});
                if(dy<0) m_pending.push_back({x,origin.y,width,int(-dy)});
            }
            const int64_t x0=FloorDivide(origin.x,CacheSize),x1=FloorDivide(origin.x+CacheSize-1,CacheSize);
            const int64_t y0=FloorDivide(origin.y,CacheSize),y1=FloorDivide(origin.y+CacheSize-1,CacheSize);
            for(int64_t y=y0;y<=y1;++y) for(int64_t x=x0;x<=x1;++x)
            {
                const Cell tile{x,y}; const int i=TileIndex(tile);
                if(newTags || !m_tags[i] || m_tiles[i]!=tile)
                { m_tiles[i]=tile; m_tags[i]=nextTag++; }
            }
            m_origin=origin; m_valid=true;
        }
        Region Next(uint32_t budget) const
        {
            budget=std::min(budget,uint32_t(CacheSize*CacheSize));
            if(m_pending.empty() || !budget) return {};
            auto r=m_pending.front();
            r.width=std::min({r.width,CacheSize-Wrap(r.x),int(budget)});
            r.height=std::min({r.height,CacheSize-Wrap(r.y),int(budget/uint32_t(r.width))});
            return r;
        }
        void Complete(Region done)
        {
            if(!done.Area() || m_pending.empty()) return;
            const auto front=m_pending.front();
            // A lease is completed before any subsequent Plan changes this queue.
            if(front.x!=done.x || front.y!=done.y) return;
            m_pending.pop_front();
            if(front.height>done.height)
                m_pending.push_front({front.x,front.y+done.height,front.width,front.height-done.height});
            if(front.width>done.width)
                m_pending.push_front({front.x+done.width,front.y,front.width-done.width,done.height});
        }
        uint64_t PendingArea() const
        { uint64_t total=0; for(auto r:m_pending) total+=r.Area(); return total; }
        const std::deque<Region>& Pending() const { return m_pending; }
        const std::array<uint32_t,4>& Tags() const { return m_tags; }
        uint32_t Tag(Cell tile) const { return m_tiles[TileIndex(tile)]==tile?m_tags[TileIndex(tile)]:0; }
    private:
        Cell m_origin{};
        std::array<Cell,4> m_tiles{};
        std::array<uint32_t,4> m_tags{};
        std::deque<Region> m_pending;
        bool m_valid=false;
    };
}
