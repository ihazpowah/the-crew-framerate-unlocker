// Standalone C++17: deliberately no adapter, platform, hook or graphics headers.
#include <cassert>
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>
#include "../../source/interpolator/Pose.h"
#include "../../source/interpolator/Session.h"
#include "../../source/interpolator/CandidateHistory.h"
#include "../../source/interpolator/DirectionHistory.h"
#include "../../source/interpolator/PresentationState.h"
#include "reference/BaselineMath.h"
#include "reference/BaselineHistory.h"
using namespace interpolator;

// Synthetic A uses row-major floats plus double world coordinates.
struct NativeA { float rows[9]; double world[3]; unsigned opaque=0x12345678; };
// Synthetic B uses column-major doubles and integer millimetres relative to an origin.
struct NativeB { double columns[9]; std::int64_t millimetres[3]; double origin[3]; unsigned opaque=0x87654321; };
static Pose Decode(const NativeA& n) { Pose p{};std::copy(n.rows,n.rows+9,p.basis);std::copy(n.world,n.world+3,p.position);return p; }
static Pose Decode(const NativeB& n) {
    Pose p{};for(int r=0;r<3;++r)for(int c=0;c<3;++c)p.basis[r*3+c]=float(n.columns[c*3+r]);
    for(int a=0;a<3;++a)p.position[a]=n.origin[a]+double(n.millimetres[a])/1000.;return p;
}
static NativeA Encode(const NativeA& n,const Pose& p) { auto out=n;std::copy(p.basis,p.basis+9,out.rows);std::copy(p.position,p.position+3,out.world);return out; }
static NativeB Encode(const NativeB& n,const Pose& p) {
    auto out=n;for(int r=0;r<3;++r)for(int c=0;c<3;++c)out.columns[c*3+r]=p.basis[r*3+c];
    for(int a=0;a<3;++a)out.millimetres[a]=std::llround((p.position[a]-out.origin[a])*1000);return out;
}
struct Track { PoseHistory history;std::int64_t seenTick=-1;std::uint64_t seenMilliseconds=0; };
using Tracks=Session<TrackKey,Track,TrackKeyHash>;
static bool Observe(Tracks& tracks,TrackKey key,const Pose& p,std::int64_t tick,double seconds,double target,std::uint64_t now,Pose& out) {
    auto& t=tracks.Observe(key);t.seenTick=tick;t.seenMilliseconds=now;
    return ObserveAndEvaluate<PoseCodec>(t.history,key.incarnation,p,tick,seconds,target,out,AffineBasis,10000.,1e-8);
}
static void Maintain(Tracks& tracks,std::int64_t tick,std::uint64_t now) {
    tracks.Sweep(64,60,now,1000,[&](const auto& track) {
        return Expired(track.seenTick,track.seenMilliseconds,tick,now,{60,1000});
    });
}
static void ContainerMutationSafety()
{
    using Store=Session<unsigned,unsigned>;
    using Map=std::unordered_map<unsigned,unsigned>;
    static_assert(!std::is_convertible<Store*,Map*>::value);
    Store store;
    for (unsigned i=0;i<1024;++i) store.Observe(i)=i;
    unsigned visits=0;
    store.Sweep(1,1024,0,1000,[&](unsigned) { ++visits; return false; });
    assert(visits==2);
    store.reserve(8192); // A partial sweep must not retain invalidated iterators.
    store.erase(store.begin());
    store.Observe(2048)=2048;
    store.rehash(16384);
    for (unsigned i=0;i<1025 && !store.empty();++i)
        store.Sweep(1,1024,0,1000,[](unsigned) { return true; });
    assert(store.empty());
    store.Observe(1)=1;
    store.clear();
    store.Observe(2)=2;
    store.Sweep(1,1,1000,1000,[](unsigned value) { return value==2; });
    assert(store.empty());

    struct Effect { bool queuedForFinalization=false; unsigned visits=0; };
    using Effects=EffectHistoryMap<unsigned,Effect>;
    static_assert(!std::is_convertible<Effects*,std::unordered_map<unsigned,Effect>*>::value);
    Effects effects;
    for (unsigned i=0;i<1024;++i) effects.emplace(i,Effect{});
    assert(effects.Sweep([](Effect&) { return false; })==256);
    effects.reserve(8192);
    effects.erase(effects.begin());
    effects.rehash(16384);
    for (unsigned i=0;i<4;++i) effects.Sweep([](Effect& effect) { ++effect.visits; return false; });
    for (const auto& item:effects) assert(item.second.visits==1);
    effects.QueueForFinalization(2048,effects[2048]);
    Effects other;
    effects.swap(other);
    other.FinalizePending([](Effect& effect) { ++effect.visits; return true; });
    assert(other.at(2048).visits==1 && !other.at(2048).queuedForFinalization);
    while (!other.empty()) other.Sweep([](Effect&) { return true; });
    assert(effects.empty());
}
static Pose Identity(double x=0) { Pose p{};p.basis[0]=p.basis[4]=p.basis[8]=1;p.position[0]=x;return p; }
static void AdaptersAndSessions() {
    Tracks a,b;const TrackKey k{1,42,7};Pose output{};
    NativeA na{};na.rows[0]=na.rows[4]=float(std::cos(.25));na.rows[1]=float(-std::sin(.25));na.rows[3]=float(std::sin(.25));na.rows[8]=1;na.world[0]=1000000.125;
    NativeB nb{};for(int r=0;r<3;++r)for(int c=0;c<3;++c)nb.columns[c*3+r]=na.rows[r*3+c];nb.origin[0]=1000000;nb.millimetres[0]=125;
    assert(!Observe(a,k,Decode(na),1,1,1,1000,output));assert(!Observe(b,k,Decode(nb),1,1,1,1000,output));
    na.world[0]+=2;nb.millimetres[0]+=2000;
    na.rows[0]=na.rows[4]=float(std::cos(.7));na.rows[1]=float(-std::sin(.7));na.rows[3]=float(std::sin(.7));
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)nb.columns[c*3+r]=na.rows[r*3+c];
    Pose pa{},pb{};assert(Observe(a,k,Decode(na),2,2,1.5,1017,pa));assert(Observe(b,k,Decode(nb),2,2,1.5,1017,pb));
    assert(!std::memcmp(pa.basis,pb.basis,sizeof(pa.basis)));assert(pa.position[0]==pb.position[0]);
    auto ea=Encode(na,pa);auto eb=Encode(nb,pb);assert(ea.opaque==na.opaque && eb.opaque==nb.opaque);
    const auto da=Decode(ea),db=Decode(eb);assert(da.position[0]==db.position[0]);assert(!std::memcmp(da.basis,db.basis,sizeof(da.basis)));
    // Same-tick correction; output lifetime is independent of subsequent observation/reset.
    const auto immutable=std::make_shared<const Pose>(pa);na.world[0]+=2;
    assert(Observe(a,k,Decode(na),2,2,1.5,1018,output));assert(output.position[0]==1000002.125);
    assert(b.at(k).history.current.position[0]==1000002.125);
    a.Reset();assert(a.empty() && b.size()==1 && immutable->position[0]==1000001.125);
    assert(!Observe(a,k,Identity(),4,4,3.5,2000,output));
    assert(Observe(a,k,Identity(4),8,8,6,2100,output));assert(output.position[0]==2); // sparse samples
    assert(!Observe(a,k,Identity(4),7,7,6,2200,output)); // rollback
    assert(!Observe(a,{1,42,8},Identity(9),8,8,7.5,2300,output));assert(a.size()==2); // incarnation
    assert(!Observe(a,{2,42,7},Identity(9),8,8,7.5,2300,output));assert(a.size()==3); // channel
    assert(a.Retire(k));assert(!a.Retire(k));assert(a.size()==2);
    // Separate clocks/sessions can use the same key without sharing time or cached poses.
    assert(!Observe(a,k,Identity(90),900,12,12,3000,output));
    assert(Observe(b,k,Identity(1000004),3,3,2.5,3000,output));assert(output.position[0]==1000003.0625);
}
static void Pruning() {
    Tracks tracks;Pose out{};const TrackKey k{1,1,1};
    for(std::int64_t tick=1;tick<=240;++tick) {
        Observe(tracks,k,Identity(),tick,tick/60.,(tick-.5)/60.,1000+tick*17,out);
        Maintain(tracks,tick,1000+tick*17);assert(tracks.size()==1);
    }
    const auto now=1000+240*17;
    Maintain(tracks,300,now+999);assert(tracks.size()==1);
    Maintain(tracks,299,now+1000);assert(tracks.size()==1);
    Maintain(tracks,300,now+1000);assert(tracks.empty());
    Observe(tracks,k,Identity(),1,1,1,1000,out);Maintain(tracks,61,999);assert(tracks.size()==1);
    // Population-scaled maintenance eventually reclaims every inactive track.
    tracks.Reset();for(unsigned i=0;i<4097;++i)Observe(tracks,{1,i,1},Identity(),1,1,1,1000,out);
    for(unsigned i=0;i<65;++i)Maintain(tracks,61,2000);assert(tracks.empty());
}
static void DifferentialMath() {
    unsigned count=0;
    for(unsigned family:{unsigned(RigidBasis),unsigned(AffineBasis)})
    for(bool mirror:{false,true})for(double shear:{0.,.01,.15})for(double angle:{-.9,0.,1.7})
    for(double alpha:{-.25,0.,.125,.5,.875,1.,1.02}) {
        float a[16]{},b[16]{},expected[16]{};a[15]=b[15]=1;
        for(int endpoint=0;endpoint<2;++endpoint) {
            auto* m=endpoint?b:a;const double t=angle+endpoint*.1;
            m[0]=float(std::cos(t));m[1]=float(-std::sin(t)+shear);m[4]=float(std::sin(t));m[5]=float(std::cos(t));m[10]=mirror?-1.f:1.f;
            m[12]=10000;m[3]=float(endpoint*.125);m[13]=20;m[7]=float(endpoint*.03);
        }
        frozen::AffineBasisCache fc;AffineBasisCache ac;frozen::RigidBasisInterpolationCache frc;RigidBasisInterpolationCache rc;
        for(unsigned repeat=0;repeat<3;++repeat) {
            int es=0,actualStatus=0;frozen::InterpolateWorldMatrix(expected,a,b,alpha,&es,family,&frc,&fc);
            Pose p{},q{},out{};for(int r=0;r<3;++r)for(int c=0;c<3;++c){p.basis[r*3+c]=a[r*4+c];q.basis[r*3+c]=b[r*4+c];}
            for(int axis=0;axis<3;++axis){p.position[axis]=double(a[12+axis])+a[3+4*axis];q.position[axis]=double(b[12+axis])+b[3+4*axis];}
            InterpolatePose(out,p,q,alpha,family,&ac,&actualStatus,&rc);assert(es==actualStatus);
            for(int r=0;r<3;++r)assert(!std::memcmp(out.basis+r*3,expected+r*4,3*sizeof(float)));
            // Encoding with the current split reference retains the baseline arithmetic order.
            float encoded[16]{};std::memcpy(encoded,b,64);
            for(int axis=0;axis<3;++axis) {
                const double high=double(b[12+axis]),low=double(b[3+4*axis]);
                frozen::WriteWorldSplitTranslation(encoded,axis,high+(out.position[axis]-(high+low)),low);
            }
            for(int r=0;r<3;++r)std::memcpy(encoded+r*4,out.basis+r*3,3*sizeof(float));
            assert(!std::memcmp(encoded,expected,64));++count;
        }
    }
    std::printf("Portable math differential: %u byte-identical cases\n",count);
}
struct NumericWindowCodec {
    static void Blend(float* out,const float* a,const float* b,double alpha,int* status,RigidBasisInterpolationCache* cache) {
        std::memcpy(out,b,12*sizeof(float));InterpolateBasis(out,a,b,alpha,status,RigidBasis,cache);
        for(int i=9;i<12;++i)out[i]=float(InterpolatePosition(a[i],b[i],alpha));
    }
    static bool ValidBasis(const float* sample) { return AffineBasisLooksValid(sample); }
};
static void WindowEvaluation() {
    SnapshotHistory<12> h{};float sample[12]{};sample[0]=sample[4]=sample[8]=1;sample[9]=2;
    AppendPresentationSnapshot(h,sample,1,10,1);sample[9]=4;
    struct Frame { std::int64_t previousTick,currentTick;double intervalStartSeconds,intervalEndSeconds,targetTimeSeconds; } frame{1,2,1,2,1.5};
    PresentationCache<12> zeroGeneration{};
    SnapshotHistory<12> empty{};
    assert(!EvaluatePresentationHistory<NumericWindowCodec>(zeroGeneration,empty,0,frame,nullptr,-1,.034,4,12));
    assert(zeroGeneration.presentedGeneration==-1);
    float first[12]{};first[0]=first[4]=first[8]=1;
    AppendPresentationSnapshot(empty,first,0,0,0);
    const Frame zeroFrame{0,1,0,1,0};
    assert(EvaluatePresentationHistory<NumericWindowCodec>(zeroGeneration,empty,0,zeroFrame,nullptr,-1,.034,4,12));
    assert(zeroGeneration.presentedGeneration==0 && zeroGeneration.presentedMatrix[0]==1);
    PresentationCache<12> cache{};
    assert(EvaluatePresentationHistory<NumericWindowCodec>(cache,h,20,frame,sample,2,.034,4,12));
    assert(cache.presentedMatrix[9]==3 && h.current[9]==2 && sample[9]==4);
    sample[9]=8; // Same generation consumes the immutable cached presentation.
    assert(EvaluatePresentationHistory<NumericWindowCodec>(cache,h,20,frame,sample,2,.034,4,12));assert(cache.presentedMatrix[9]==3);
    assert(!EvaluatePresentationHistory<NumericWindowCodec>(cache,h,21,frame,sample,3,.034,4,12));
    frame.targetTimeSeconds=2.0341;assert(!EvaluatePresentationHistory<NumericWindowCodec>(cache,h,21,frame,sample,2,.034,4,12));
    SnapshotHistory<12,6> wider{};
    for(int i=1;i<=10;++i){sample[9]=float(i);AppendPresentationSnapshot(wider,sample,i,i,double(i));}
    assert(wider.olderCount==4 && wider.olderStamps[0].tick==5 && wider.currentTick==10);
    SafePresentationStamp keys[6];const float* values[6];
    assert(BuildPresentationView(wider,nullptr,11,11,11,keys,values)==6);
    assert(keys[0].tick==5 && values[5][9]==10);
    assert(BuildPresentationView(wider,sample,11,11,11,keys,values)==6 && keys[0].tick==6 && keys[5].tick==11);
}
static void CohortsAndDirections() {
    CandidateHistory<12> absent{};float held[12]{};held[0]=held[4]=held[8]=1;
    ObservePresentationCandidate(absent,held,10,1,1,12);
    assert(FinalizeCohort(absent,1,11,1));assert(!absent.finalizedSnapshots.hasCurrent && absent.nativeCandidates.empty());
    ObservePresentationCandidate(absent,held,20,2,2,12);assert(!FinalizeCohort(absent,1,10,1));
    assert(FinalizeCohort(absent,2,20,2) && absent.finalizedSnapshots.currentTick==2);
    CandidateHistory<12> slot{};float sample[12]{};sample[0]=sample[4]=sample[8]=1;
    ObservePresentationCandidate(slot,sample,10,1,1,12);sample[9]=2;
    ObservePresentationCandidate(slot,sample,11,1,1,12);assert(slot.nativeCandidates.size()==2);
    assert(FinalizeCohort(slot,1,11,1));assert(slot.finalizedSnapshots.current[9]==2);
    assert(FinalizeCohort(slot,2,12,2));assert(slot.finalizedSnapshots.currentTick==1); // absent cohort
    sample[9]=4;ObservePresentationCandidate(slot,sample,13,3,3,12);FinalizeCohort(slot,3,13,3);
    for(int i=4;i<20;++i){sample[9]=float(i);AppendPresentationSnapshot(slot.finalizedSnapshots,sample,i,i,double(i));}
    assert(slot.finalizedSnapshots.olderCount==2); // four-key cap
    SafePresentationStamp stamps[4]{{1,1,1},{2,2,2},{3,3,3},{4,4,4}};SafePresentationInterval interval;
    assert(SelectSafePresentationInterval(stamps,4,4.034,&interval,.034,4));
    assert(!SelectSafePresentationInterval(stamps,4,4.0341,&interval,.034,4));
    stamps[2].tick=1;assert(!SelectSafePresentationInterval(stamps,4,2.5,&interval,.034,4));
    DirectionHistory<16> solar{};float direction[3]{1,0,0},out[3]{};
    assert(!SampleDirection(solar,direction,1,1,out,.125,.25));
    for(int i=2;i<40;++i){direction[0]=float(std::cos(i*.001));direction[1]=float(std::sin(i*.001));SampleDirection(solar,direction,i*.08+1,i,out,.125,.25);}
    assert(solar.count==16);const auto before=solar;
    Tracks physics;Pose p{};Observe(physics,{1,1,1},Identity(),10000,900,900,1000,p);
    assert(!std::memcmp(solar.keys.data(),before.keys.data(),sizeof(solar.keys)));
    direction[0]=0;direction[1]=1;assert(!SampleDirection(solar,direction,9,40,out,.125,.25));assert(solar.count==1);
    direction[0]=std::numeric_limits<float>::quiet_NaN();assert(!SampleDirection(solar,direction,10,41,out,.125,.25));assert(!solar.count);
}
static void DifferentialHistories() {
    unsigned cases=0;
    for(unsigned retained=0;retained<=20;++retained) {
        SnapshotHistory<16> actual{};frozenHistory::PresentationSnapshotHistory expected{};
        float sample[16]{};sample[0]=sample[5]=sample[10]=1;
        for(unsigned i=0;i<retained;++i) {
            sample[12]=float(i);sample[3]=float(i*.001);sample[15]=float(100+i);
            AppendPresentationSnapshot(actual,sample,i+1,i+100,(i+1)/60.);
            frozenHistory::AppendPresentationSnapshot(expected,sample,i+1,i+100,(i+1)/60.);
        }
        assert(actual.hasCurrent==expected.hasCurrent && actual.hasPrevious==expected.hasPrevious && actual.olderCount==expected.olderCount);
        assert(actual.currentTick==expected.currentTick && actual.previousTick==expected.previousTick);
        assert(actual.currentGeneration==expected.currentGeneration && actual.previousGeneration==expected.previousGeneration);
        assert(!std::memcmp(actual.current,expected.current,sizeof(actual.current)) && !std::memcmp(actual.previous,expected.previous,sizeof(actual.previous)));
        assert(!std::memcmp(actual.older,expected.older,sizeof(actual.older)));
        for(unsigned i=0;i<actual.olderCount;++i) {
            assert(actual.olderStamps[i].tick==expected.olderStamps[i].tick && actual.olderStamps[i].generation==expected.olderStamps[i].generation && actual.olderStamps[i].timeSeconds==expected.olderStamps[i].timeSeconds);
        }
        for(int mode=0;mode<5;++mode)for(double target:{-.1,0.,.015,.08,.15,.25,.35,.4}) {
            const float* live=mode?sample:nullptr;
            const auto tick=mode==1?actual.currentTick:std::int64_t(retained+mode);
            const double seconds=mode==2?actual.currentTimeSeconds:double(tick)/60.;
            SafePresentationStamp a[4];frozenHistory::SafePresentationStamp b[4];const float* av[4];const float* bv[4];
            auto ac=BuildPresentationView(actual,live,tick,999,seconds,a,av);
            auto bc=frozenHistory::BuildPresentationView(expected,live,tick,999,seconds,b,bv);assert(ac==bc);
            for(unsigned i=0;i<ac;++i){assert(a[i].tick==b[i].tick && a[i].generation==b[i].generation && a[i].timeSeconds==b[i].timeSeconds);assert(!std::memcmp(av[i],bv[i],64));}
            SafePresentationInterval ai;frozenHistory::SafePresentationInterval bi;
            const bool ok=SelectSafePresentationInterval(a,ac,target,&ai,.034,4);
            assert(ok==frozenHistory::SelectSafePresentationInterval(b,bc,target,&bi));
            if(ok)assert(ai.first==bi.first && ai.second==bi.second && ai.alpha==bi.alpha);
            ++cases;
        }
    }
    std::printf("Portable history differential: %u identical windows/intervals\n",cases);
}
static void IntegerGridAndOwnedHistories() {
    // QPC-derived tick identities can exceed exact double integer precision.
    // Selection must subtract on the integer grid before converting the ratio.
    const std::int64_t base = 9007199254740993LL;
    const std::int64_t times[] = {base,base+166667,base+333334};
    PresentationInterval interval;
    const auto at = [&](unsigned i) { return times[i]; };
    assert(SelectPresentationInterval(3,base+166667,std::int64_t(340000),std::int64_t(0),at,&interval));
    assert(interval.first == 1 && interval.second == 1 && interval.alpha == 0);
    assert(SelectPresentationInterval(3,base+83333,std::int64_t(340000),std::int64_t(0),at,&interval));
    assert(interval.alpha == 83333.0/166667.0);
    assert(SelectPresentationInterval(3,base+333334+340000,std::int64_t(340000),std::int64_t(0),at,&interval));
    assert(!SelectPresentationInterval(3,base+333334+340001,std::int64_t(340000),std::int64_t(0),at,&interval));
    assert(!SelectPresentationInterval(3,base-1,std::int64_t(340000),std::int64_t(0),at,&interval));

}
int main() {
    ContainerMutationSafety();
    IntegerGridAndOwnedHistories();
    AdaptersAndSessions();Pruning();DifferentialMath();DifferentialHistories();WindowEvaluation();CohortsAndDirections();
    std::puts("PASS: portable core, adapters, isolated sessions, histories, identities, pruning and clocks");
}
