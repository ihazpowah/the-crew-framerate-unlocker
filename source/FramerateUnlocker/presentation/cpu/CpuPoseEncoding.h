#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
static bool CpuBasisAdmitted(const float* world,const CpuPoseHistory* history)
{
    if (!history || !history->basisAdmitted) return false;
    for (int r=0;r<3;++r) if (memcmp(world+r*4,history->current+r*4,3*sizeof(float))) return false;
    return true;
}
static bool NativeTransformToWorldMatrix(const NativeTransformResource& pose,float* world,const CpuPoseHistory* history=nullptr)
{
    memset(world,0,64); world[15]=1;
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) world[r*4+c]=pose.matrix[c*4+r];
    for (int a=0;a<3;++a)
    { world[3+a*4]=pose.matrix[12+a]; world[12+a]=pose.low[a]; }
    for (float value: std::array<float,3>{float(WorldMatrixPosition(world,0)),float(WorldMatrixPosition(world,1)),float(WorldMatrixPosition(world,2))})
        if (!Finite(value) || std::fabs(value)>10000000.f) return false;
    return CpuBasisAdmitted(world,history) || AffineBasisLooksValid(world);
}
static NativeTransformResource WorldMatrixToNativeTransform(const NativeTransformResource& native,const float* world)
{
    NativeTransformResource result=native;
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) result.matrix[r*4+c]=world[c*4+r];
    for (int a=0;a<3;++a)
    { result.matrix[12+a]=world[3+a*4]; result.low[a]=world[12+a]; }
    return result;
}
static NativeTransformResource CpuWheelPose(const CpuWheelResource& resource)
{
    NativeTransformResource pose{};
    memcpy(pose.matrix,resource.bytes+0x40,64);
    memcpy(pose.low,resource.bytes+0x90,16);
    // Wheel matrices carry the low position; the separate vector is high.
    for (int a=0;a<3;++a) std::swap(pose.matrix[12+a],pose.low[a]);
    return pose;
}
static CpuWheelResource CpuWheelFromWorld(const CpuWheelResource& native,const float* world)
{
    CpuWheelResource result=native;
    const auto pose=WorldMatrixToNativeTransform(CpuWheelPose(native),world);
    // Preserve all opaque lanes and wheel material/geometry fields.
    for (int r=0;r<3;++r) for (int c=0;c<3;++c)
        memcpy(result.bytes+0x40+(r*4+c)*4,pose.matrix+r*4+c,4);
    for (int a=0;a<3;++a)
    {
        memcpy(result.bytes+0x70+a*4,pose.low+a,4);
        memcpy(result.bytes+0x90+a*4,pose.matrix+12+a,4);
    }
    return result;
}
struct CpuOwnedPoseCodec {
    using Input=const float*;
    using Output=float*;
    static void Copy(float* out,const float* in) { memcpy(out,in,64); }
    static double Position(const float* value,int axis) { return WorldMatrixPosition(value,axis); }
    static void Blend(float* out,const float* previous,const float* current,double alpha,unsigned family,AffineBasisCache& cache)
    { InterpolateWorldMatrix(out,previous,current,alpha,nullptr,family,nullptr,&cache); }
};
static bool CpuInterpolate(CpuPoseHistory& history,uint64_t identity,const float* native,
    LONG64 tick,double timeSeconds,double target,float* output,unsigned basisFamily)
{
    return interpolator::ObserveAndEvaluate<CpuOwnedPoseCodec>(history,identity,native,tick,timeSeconds,target,output,basisFamily,TheCrewInterpolationPolicy::poseDiscontinuitySquared,TheCrewInterpolationPolicy::endpointEpsilonSeconds);
}
