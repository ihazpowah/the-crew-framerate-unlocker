#pragma once

// Independent native minimap history. Never enters scene/GPU skew processing.
namespace MinimapPresentation
{
struct Sample
{
    uintptr_t owner=0,source=0,state=0,subsystem=0,service=0;
    unsigned thread=0;
    LONG64 generation=-1,tick=-1;
    double step=0,alpha=0;
    ULONGLONG milliseconds=0;
    float pose[16]{};
};
struct History
{
    Sample current{};
    float previous[16]{},output[16]{};
    bool seeded=false,ready=false,presented=false;
    ULONGLONG tickMilliseconds=0;
};
static bool ValidPose(const float* pose)
{
    for(int i=0;i<16;++i) if(!std::isfinite(pose[i])) return false;
    if(pose[3]!=0 || pose[7]!=0 || pose[11]!=0 || pose[15]!=1) return false;
    float basis[9]{};double scale[3]{};bool mirrored=false;
    if(!SceneMtxRotationBasis(pose,basis,scale,&mirrored) || mirrored) return false;
    for(double s:scale) if(std::fabs(s-1)>0.002) return false;
    for(int i=12;i<15;++i) if(std::fabs(pose[i])>10000000) return false;
    return true;
}
static Quaternion Rotation(const float* pose)
{
    float basis[9]{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)basis[r*3+c]=pose[r*4+c];
    return SceneQuaternionFromBasis(basis);
}
static bool Present(History& h,const Sample& s,float* output)
{
    if(!s.owner || !s.source || !s.state || !s.subsystem || !s.service || !s.thread ||
        s.tick<=0 || s.generation<0 || !std::isfinite(s.step) || s.step<0.001 || s.step>0.05 ||
        !std::isfinite(s.alpha) || s.alpha<0 || s.alpha>=1 || !ValidPose(s.pose))
    { h={};return false; }
    const auto seed=[&](){h={};h.current=s;h.seeded=true;h.tickMilliseconds=s.milliseconds;};
    const auto& c=h.current;
    if(!h.seeded || s.owner!=c.owner || s.source!=c.source || s.state!=c.state ||
        s.subsystem!=c.subsystem || s.service!=c.service || s.thread!=c.thread ||
        s.generation<c.generation || s.generation-c.generation>1 ||
        s.tick<c.tick || s.tick-c.tick>1 || std::fabs(s.step-c.step)>1e-7 ||
        s.milliseconds<c.milliseconds || s.milliseconds-h.tickMilliseconds>100)
    { seed();return false; }
    if(s.generation==c.generation)
    {
        if(s.tick!=c.tick || memcmp(s.pose,c.pose,64)) {seed();return false;}
        if(h.presented) memcpy(output,h.output,64);
        return h.presented;
    }
    if(s.tick==c.tick)
    {
        if(memcmp(s.pose,c.pose,64) || s.alpha+1e-6<c.alpha) {seed();return false;}
    }
    else
    {
        double distance=0;
        for(int i=12;i<15;++i){const double d=double(s.pose[i])-c.pose[i];distance+=d*d;}
        if(distance>400 || std::fabs(SceneQuaternionDot(Rotation(s.pose),Rotation(c.pose)))<0.7071067f)
        {seed();return false;}
        memcpy(h.previous,c.pose,64);h.ready=true;h.tickMilliseconds=s.milliseconds;
    }
    h.current=s;h.presented=false;
    if(!h.ready) return false;
    if(!memcmp(h.previous,s.pose,64))
    {memcpy(output,s.pose,64);memcpy(h.output,output,64);h.presented=true;return true;}
    Quaternion a=Rotation(h.previous),b=Rotation(s.pose);
    if(SceneQuaternionDot(a,b)<0) b=SceneQuaternionNegate(b);
    const float t=static_cast<float>(s.alpha),u=1-t;
    const Quaternion q=SceneQuaternionNormalize({u*a.W+t*b.W,u*a.X+t*b.X,u*a.Y+t*b.Y,u*a.Z+t*b.Z});
    float basis[9]{};SceneQuaternionToBasis(q,basis);
    memcpy(output,s.pose,64);
    for(int r=0;r<3;++r)for(int col=0;col<3;++col)output[r*4+col]=basis[r*3+col];
    for(int i=12;i<15;++i)output[i]=static_cast<float>(double(h.previous[i])+(double(s.pose[i])-h.previous[i])*s.alpha);
    memcpy(h.output,output,64);h.presented=true;return true;
}
}
