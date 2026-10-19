#pragma once
#include "Basis.h"
namespace interpolator {
template<class Sample,std::size_t Lanes> struct PresentationState
{
    Sample current{};
    float previous[Lanes]{},output[Lanes]{};
    bool seeded=false,ready=false,presented=false;
    std::uint64_t tickMilliseconds=0;
};

template<int Stride>
static Quaternion Rotation(const float* pose)
{
    float basis[9]{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)basis[r*3+c]=pose[r*Stride+c];
    return QuaternionFromBasis(basis);
}

template<int Stride=3> static void InterpolateNormalizedRotation(float* output,const float* previous,const float* current,double alpha)
{
    Quaternion a=Rotation<Stride>(previous),b=Rotation<Stride>(current);
    if(QuaternionDot(a,b)<0) b=QuaternionNegate(b);
    const float t=static_cast<float>(alpha),u=1-t;
    const Quaternion q=SceneQuaternionNormalize({u*a.W+t*b.W,u*a.X+t*b.X,u*a.Y+t*b.Y,u*a.Z+t*b.Z});
    float basis[9]{};QuaternionToBasis(q,basis);

    for(int r=0;r<3;++r)for(int col=0;col<3;++col)output[r*Stride+col]=basis[r*3+col];


}
}
