#pragma once
namespace IntegratedController {
template<class Slots>
static bool PruneLightDeliverySlots(Slots& slots,unsigned& count,LONG64 tick,ULONGLONG now)
{
    if (count>slots.size()) return false; // Corrupt bookkeeping stays fail-closed at consumption.
    struct Scan { const void* owner=nullptr; size_t cursor=0; };
    // Current/previous vectors share a slot type, so keep independent cursors.
    static Scan scans[2];
    Scan* scan=&scans[0];
    if(scan->owner && scan->owner!=&slots) scan=&scans[1];
    if(scan->owner!=&slots) *scan={&slots,0};
    bool expired=false;
    const size_t budget=std::max<size_t>(512,(size_t(count)+15)/16);
    const size_t checks=std::min<size_t>(count,budget);
    for(size_t n=0;n<checks;++n)
    {
        if(scan->cursor>=count) scan->cursor=0;
        expired|=slots[scan->cursor++].seen.Expired(tick,now);
    }
    if(!expired && slots.capacity()<=std::max<size_t>(256,size_t(count)*4)) return false;
    unsigned kept=0;
    for (unsigned i=0;i<count;++i) if (!slots[i].seen.Expired(tick,now)) ++kept;
    scan->cursor=0;
    Slots compact;
    for (unsigned i=0;i<count;++i) if (!slots[i].seen.Expired(tick,now)) compact.emplace_back()=slots[i];
    slots=std::move(compact); count=kept; return true;
}
static void PruneReferenceEffectHistories(LONG64 tick,ULONGLONG now)
{
    PruneEffectHistories(tick,now);
    PruneEffectMap(g_shadowCameras,[&](const auto& h){return h.seen.Expired(tick,now);});
    {
        namespace A=AmbientProjectorPresentation;
        if (PruneLightDeliverySlots(A::records,A::count,tick,now))
        {
            decltype(A::index)().swap(A::index); A::providerIndex.Release(); A::payloadIndex.Release();
            for(unsigned i=0;i<A::count;++i) if(A::records[i].queued)
            { A::Insert(i,false); if(A::records[i].correctedReady) A::Insert(i,true); }
        }
        if (PruneLightDeliverySlots(A::previousRecords,A::previousCount,tick,now)) { A::previousPayloadIndex.Release(); A::previousProviderIndex.Release(); }
    }
    {
        namespace P=PointLightPresentation;
        if (PruneLightDeliverySlots(P::records,P::count,tick,now))
        {
            decltype(P::index)().swap(P::index); P::providerIndex.Release(); P::payloadIndex.Release();
            for(unsigned i=0;i<P::count;++i) if(P::records[i].queued)
            { P::Insert(i,false); if(P::records[i].correctedReady) P::Insert(i,true); }
        }
        if (PruneLightDeliverySlots(P::previousRecords,P::previousCount,tick,now)) { P::previousPayloadIndex.Release(); P::previousProviderIndex.Release(); }
    }
}
}
