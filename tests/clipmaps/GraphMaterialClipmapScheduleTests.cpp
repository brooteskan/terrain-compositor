#include "GraphMaterialClipmapSchedule.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <set>
using namespace TerrainCompositor::GraphClipmaps;
void Check(bool value,const char* message) { if(!value) { std::cerr<<message<<'\n';std::exit(1); } }
int main()
{
    uint32_t tag=1; Schedule schedule;
    std::map<std::pair<int,int>,std::pair<Cell,uint32_t>> texture;
    std::mt19937 random(92873); Cell origin{-260,-268};
    size_t jobs=0;
    for(int frame=0;frame<900;++frame)
    {
        if(frame%7==0) { origin.x+=int(random()%65)-32; origin.y+=int(random()%65)-32; }
        if(frame==300) origin={-8503,9201};
        const bool invalid=frame==500;
        schedule.Plan(origin,invalid,false,tag);
        std::array<unsigned char,512*512> dirty{};
        for(auto r:schedule.Pending()) for(int y=0;y<r.height;++y) for(int x=0;x<r.width;++x)
        {
            Check(r.x+x>=origin.x && r.x+x<origin.x+512 && r.y+y>=origin.y && r.y+y<origin.y+512,"pending region escaped window");
            auto& mark=dirty[size_t(r.y+y-origin.y)*512+size_t(r.x+x-origin.x)];
            Check(!mark,"duplicate pending texel");mark=1;
        }
        const uint32_t budget=frame%13==0?31:16384;
        auto job=schedule.Next(budget);
        Check(job.Area()<=budget,"budget exceeded");
        if(job.Area())
        {
            Check(Wrap(job.x)+job.width<=512 && Wrap(job.y)+job.height<=512,"job crosses torus seam");
            Cell tile{FloorDivide(job.x,512),FloorDivide(job.y,512)};
            for(int y=0;y<job.height;++y) for(int x=0;x<job.width;++x)
                texture[{Wrap(job.x+x),Wrap(job.y+y)}]={{job.x+x,job.y+y},schedule.Tag(tile)};
            schedule.Complete(job);++jobs;
        }
        // Every apparently valid cell must contain precisely this world cell,
        // even while scrolling outruns the budget and old contents are recycled.
        for(int i=0;i<400;++i)
        {
            Cell cell{origin.x+random()%512,origin.y+random()%512};
            auto entry=texture.find({Wrap(cell.x),Wrap(cell.y)});
            if(entry==texture.end())continue;
            Cell tile{FloorDivide(cell.x,512),FloorDivide(cell.y,512)};
            if(entry->second.second==schedule.Tag(tile)) Check(entry->second.first==cell,"stale recycled texel accepted");
        }
    }
    while(schedule.PendingArea()) { auto job=schedule.Next(8192);Check(job.Area()>0,"scheduler stuck");schedule.Complete(job); }
    schedule.Plan(origin,false,false,tag);Check(!schedule.PendingArea(),"idle cache updated");
    origin.x+=16; schedule.Plan(origin,false,false,tag);Check(schedule.PendingArea()==16*512,"scroll did not produce exact strip");
    Check(!schedule.Next(0).Area(),"zero budget scheduled work");
    Check(schedule.Next(UINT32_MAX).Area()<=512*512,"large budget overflowed");
    const auto oldTags=schedule.Tags();schedule.Plan(origin,false,true,tag);
    Check(schedule.PendingArea()==512*512 && oldTags==schedule.Tags(),"coverage refill discarded valid material texels");
    std::cout<<"900 budgeted scrolling frames, "<<jobs<<" jobs, negative seams, teleport, invalidation and stale-tag checks passed\n";
}
