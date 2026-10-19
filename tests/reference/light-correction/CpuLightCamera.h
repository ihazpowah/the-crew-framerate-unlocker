#pragma once

namespace IntegratedController
{
// Native camera constructor 0x1632A90: q_view = q_camera * q_x(+90 degrees),
// followed by the transposed view matrix at +E0. Light presentation stores its
// transpose (camera-to-world), with the original high/low position split.
// Keep this separate from the raw pose basis used by shadows and other consumers.
static void CpuLightCameraWorld(const NativeCameraPose& pose,float* world)
{
    constexpr float half=0.70710676908493042f; // sin/cos(float(pi/4))
    const float x=half*pose.rotation[0]+half*pose.rotation[3];
    const float y=half*pose.rotation[1]+half*pose.rotation[2];
    const float z=half*pose.rotation[2]-half*pose.rotation[1];
    const float w=half*pose.rotation[3]-half*pose.rotation[0];
    const float xx=(2*x)*x, yy=(2*y)*y, zz=(2*z)*z;
    const float xy=(2*x)*y, xz=(2*x)*z, yz=(2*y)*z;
    const float wx=(2*x)*w, wy=(2*y)*w, wz=(2*z)*w;
    // Match native construction without silently normalizing the input. The
    // existing finite/rigid and exact camera-provenance guards retain authority.
    memset(world,0,64);
    world[0]=(1-yy)-zz;world[1]=xy-wz;world[2]=wy+xz;
    world[4]=xy+wz;world[5]=(1-xx)-zz;world[6]=yz-wx;
    world[8]=xz-wy;world[9]=yz+wx;world[10]=(1-xx)-yy;
    for(unsigned a=0;a<3;++a){world[12+a]=pose.high[a];world[a*4+3]=pose.low[a];}
    world[15]=1;
}
}
