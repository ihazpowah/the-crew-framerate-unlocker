#pragma once
#include "Basis.h"
namespace interpolator {
inline double InterpolatePosition(double previous,double current,double alpha)
{ return previous+(current-previous)*alpha; }
inline void InterpolatePose(Pose& out,const Pose& previous,const Pose& current,double alpha,
    unsigned family=RigidBasis,AffineBasisCache* affineCache=nullptr,int* status=nullptr,RigidBasisInterpolationCache* rigidCache=nullptr)
{
    out=current;
    if(!std::memcmp(previous.basis,current.basis,sizeof(previous.basis)) &&
       !std::memcmp(previous.position,current.position,sizeof(previous.position)))
    { if(status)*status=BasisUnchanged; return; }
    for(int axis=0;axis<3;++axis)out.position[axis]=InterpolatePosition(previous.position[axis],current.position[axis],alpha);
    InterpolateBasis<3,false>(out.basis,previous.basis,current.basis,alpha,status,family,rigidCache,affineCache);
}
struct PoseCodec {
    using Input=const Pose&;
    using Output=Pose&;
    static double Position(const Pose& p,int axis) { return p.position[axis]; }
    static void Copy(Pose& out,const Pose& in) { out=in; }
    static void Blend(Pose& out,const Pose& previous,const Pose& current,double alpha,unsigned family,AffineBasisCache& cache)
    { InterpolatePose(out,previous,current,alpha,family,&cache); }
};
}
