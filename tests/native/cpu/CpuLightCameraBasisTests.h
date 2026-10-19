#pragma once
namespace CpuLightCameraBasisTests
{
using namespace IntegratedController;
static void CheckPose(const NativeCameraPose& pose)
{
    namespace A=AmbientProjectorPresentation;
    float native[16],world[16],oldWorld[16];
    NativeLightViewOracle::View(pose,native);CpuLightCameraWorld(pose,world);CameraPoseToWorldMatrix(pose,oldWorld);
    assert(A::Rigid(native) && A::Rigid(world));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
        assert(std::fabs(double(native[row*4+col])-world[col*4+row])<.000001);
    for(unsigned a=0;a<3;++a)
        assert(world[12+a]==pose.high[a] && world[a*4+3]==pose.low[a]);
    const double origin[]={double(pose.high[0])+pose.low[0],double(pose.high[1])+pose.low[1],double(pose.high[2])+pose.low[2]};
    A::Camera view{};view.nativeVariantCount=1;memcpy(view.nativeVariants[0],world,64);
    assert(A::MatchNativeCamera(native,origin,view));
    memcpy(view.nativeVariants[0],oldWorld,64);
    assert(!A::MatchNativeCamera(native,origin,view)); // Reproduces the observed 90-degree rejection.
    memcpy(view.nativeVariants[0],world,64);
    double foreign[3];memcpy(foreign,origin,sizeof(origin));foreign[1]+=.01;
    assert(!A::MatchNativeCamera(native,foreign,view));
    native[0]+=.01f;assert(!A::MatchNativeCamera(native,origin,view));
}
static void Check()
{
    NativeCameraPose pose{};pose.rotation[3]=1;
    pose.high[0]=94800;pose.high[1]=-32400;pose.high[2]=96;
    pose.low[0]=.00390625f;pose.low[1]=-.001953125f;pose.low[2]=.03125f;
    const auto original=pose;
    float world[16];CpuLightCameraWorld(pose,world);
    // Identity pose: native camera axes +X,+Y,+Z map to raw +X,+Z,-Y.
    const float expected[]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)assert(std::fabs(world[r*4+c]-expected[r*4+c])<.000001);
    assert(!memcmp(&pose,&original,sizeof(pose)));CheckPose(pose);
    for(unsigned axis=0;axis<3;++axis)
    {
        memset(pose.rotation,0,16);pose.rotation[axis]=float(std::sin(.37));pose.rotation[3]=float(std::cos(.37));
        CheckPose(pose);
    }
    // Mixed quaternion independently checks right multiplication with ordinary
    // matrix algebra. Premultiplying Rx would exchange different rows instead.
    const float q[]={.21f,.37f,-.13f,.89f};
    double norm=0;for(float v:q)norm+=double(v)*v;norm=std::sqrt(norm);
    for(unsigned i=0;i<4;++i)pose.rotation[i]=float(q[i]/norm);
    CheckPose(pose);CpuLightCameraWorld(pose,world);
    const double x=pose.rotation[0],y=pose.rotation[1],z=pose.rotation[2],w=pose.rotation[3];
    const double raw[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
        {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
        {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    bool differsFromWrongOrder=false;
    for(unsigned r=0;r<3;++r)
    {
        assert(std::fabs(world[r*4]-raw[r][0])<.000001);
        assert(std::fabs(world[r*4+1]-raw[r][2])<.000001);
        assert(std::fabs(world[r*4+2]+raw[r][1])<.000001);
        for(unsigned c=0;c<3;++c)
        {
            const double wrong=r==0 ? raw[0][c] : r==1 ? -raw[2][c] : raw[1][c];
            differsFromWrongOrder=differsFromWrongOrder || std::fabs(world[r*4+c]-wrong)>.1;
        }
    }
    assert(differsFromWrongOrder);
    for(float& v:pose.rotation)v=-v;CheckPose(pose);
    // Native construction does not normalize input; small valid norm drift
    // must still match the original SSE result at the existing tight tolerance.
    for(float& v:pose.rotation)v*=1.000005f;CheckPose(pose);
    uint32_t seed=0x1a7f82;
    for(unsigned sample=0;sample<256;++sample)
    {
        double length=0;
        for(float& v:pose.rotation){seed=seed*1664525+1013904223;v=float(int(seed>>8)-0x800000)/8388608.f;length+=double(v)*v;}
        length=std::sqrt(length);for(float& v:pose.rotation)v=float(v/length);
        CheckPose(pose);
    }
    pose.rotation[0]=NAN;CpuLightCameraWorld(pose,world);assert(!AmbientProjectorPresentation::Rigid(world));
    puts("PASS: native SSE camera oracle, +X90 quaternion postmultiply, identity/yaw/pitch/roll/mixed axes, sign/norm drift, 256 orientations, split precision, foreign-camera rejection and old-basis regression");
}
}
