#pragma once
#include <xmmintrin.h>

namespace NativeLightViewOracle
{
// Independent lift of original SSE at 0x1632BCC..0x1632C4D. Fixed quaternion
// comes from native +X axis and float pi/4 half-angle constants. This preserves
// the native shuffle/sign/add sequence rather than calling production helpers.
static void Quaternion(const float* input,float* output)
{
    const __m128 f=_mm_set_ps(0.70710676908493042f,0,0,0.70710676908493042f);
    const __m128 sign=_mm_set1_ps(-0.0f),q=_mm_loadu_ps(input);
    __m128 a=_mm_shuffle_ps(f,f,0x1b);
    __m128 x=_mm_shuffle_ps(q,q,0x00),z=_mm_shuffle_ps(q,q,0xaa),y=_mm_shuffle_ps(q,q,0x55);
    const __m128 negX=_mm_xor_ps(sign,x);
    x=_mm_movehl_ps(x,negX);x=_mm_shuffle_ps(x,x,0x22);
    const __m128 negZ=_mm_xor_ps(sign,z);z=_mm_movehl_ps(z,negZ);z=_mm_shuffle_ps(z,z,0x28);
    const __m128 negY=_mm_xor_ps(sign,y);y=_mm_movelh_ps(y,negY);
    a=_mm_mul_ps(a,x);
    a=_mm_add_ps(a,_mm_mul_ps(_mm_shuffle_ps(q,q,0xff),f));
    a=_mm_add_ps(a,_mm_mul_ps(y,_mm_shuffle_ps(f,f,0x4e)));
    a=_mm_add_ps(a,_mm_mul_ps(_mm_shuffle_ps(f,f,0xb1),z));
    _mm_storeu_ps(output,a);
}
// Native +E0 stores the following view rows (0x1632C4D..0x1632E65).
static void View(const NativeCameraPose& pose,float* matrix)
{
    float v[4];Quaternion(pose.rotation,v);
    const float x=v[0],y=v[1],z=v[2],w=v[3];
    memset(matrix,0,64);
    matrix[0]=(1-2*y*y)-2*z*z;matrix[1]=2*x*y+2*z*w;matrix[2]=2*x*z-2*y*w;
    matrix[4]=2*x*y-2*z*w;matrix[5]=(1-2*x*x)-2*z*z;matrix[6]=2*y*z+2*x*w;
    matrix[8]=2*y*w+2*x*z;matrix[9]=2*y*z-2*x*w;matrix[10]=(1-2*x*x)-2*y*y;
    matrix[15]=1;
}
}
