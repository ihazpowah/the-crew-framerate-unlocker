#pragma once
#include "../../reference/VehicleBasisCacheReference.h"
static void CheckAffineBasisCache()
{
    AffineBasisCache cache;
    float previous[16]{},current[16]{};
    const auto compare=[&](double alpha)
    {
        float actual[16],reference[16];
        for(int i=0;i<16;++i)actual[i]=reference[i]=float(100+i);
        const bool a=InterpolateAffineBasis(actual,previous,current,alpha,&cache);
        const bool b=ReferenceVehicleLocalBasis(reference,previous,current,alpha);
        assert(a==b && !memcmp(actual,reference,sizeof(actual)));
    };
    for(unsigned variant=0;variant<600;++variant)
    {
        for(float* p:{previous,current})
        {
            const double a=(variant+(p==current ? .3 : 0))*.017;
            memset(p,0,64);p[0]=float(cos(a));p[1]=float(-sin(a));
            p[4]=float(sin(a));p[5]=float(cos(a));p[10]=1;p[15]=1;
            p[2]=float(variant%9)*.016f;p[6]=-.035f;p[9]=.014f;
            if(variant%3==0)for(int i=0;i<3;++i)p[i]=-p[i];
        }
        for(double a:{-.25,0.,.125,.5,.875,1.,1.25})compare(a);
        // Same-tick correction must invalidate every authored basis lane.
        for(int r=0;r<3;++r)for(int c=0;c<3;++c){current[r*4+c]+=.002f;compare(.37);}
        previous[12]=100000;current[3]=.003f;compare(.83); // excluded key lanes
        memcpy(previous,current,64);current[6]+=.013f;compare(.62); // rollover reuse
        memcpy(previous,current,64);current[12]+=2;
        for(double a:{0.,.125,.5,.875,1.})compare(a); // translation-only cached output
        current[0]=current[1]=current[2]=0;compare(.4);compare(.6); // cached failure
    }
    memset(previous,0,64);memset(current,0,64);
    previous[0]=previous[5]=previous[10]=current[0]=current[5]=current[10]=1;
    current[0]=-1;compare(.5); // handedness mismatch
    current[0]=std::numeric_limits<float>::infinity();compare(.5);
    current[0]=std::numeric_limits<float>::quiet_NaN();compare(.5);
    current[0]=1;compare(std::numeric_limits<double>::quiet_NaN());
    compare(std::numeric_limits<double>::infinity());

    // Admission reuse is keyed to all nine basis values. Positions must still
    // be checked even when orientation/scale are unchanged.
    using namespace IntegratedController;
    CpuPoseHistory admitted;
    CpuBoneMatrix bone{};bone.values[0]=bone.values[5]=bone.values[10]=1;
    assert(CpuBoneWorld(bone,admitted.current));admitted.basisAdmitted=true;
    for(unsigned i=0;i<400;++i)
    {
        auto input=bone;
        input.values[(i%3)*4+(i/3)%3]+=float(i%7)*.013f;
        if(i%11==0)input.values[0]=0;
        if(i%17==0)input.values[12]=std::numeric_limits<float>::infinity();
        if(i%19==0)input.values[5]=std::numeric_limits<float>::quiet_NaN();
        float cached[16],fresh[16];
        assert(CpuBoneWorld(input,cached,&admitted)==CpuBoneWorld(input,fresh));
        assert(!memcmp(cached,fresh,sizeof(cached)));
    }
    float output[16];
    float native[16];memcpy(native,admitted.current,64);
    CpuInterpolate(admitted,9,native,10,.1,.09,output,AffineBasis);
    assert(!admitted.basisAdmitted); // identity reseed clears the proof
}
