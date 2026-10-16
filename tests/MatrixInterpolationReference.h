#pragma once
// Frozen pre-six-point scalar kernels. Regression-only; retain these when
// changing production kernels so comparisons never silently rebase.
static bool ReferenceTransformLooksValid(const float* matrix, bool gpuPose = false)
{
    double lengths[3];
    for (int i = 0; i < (gpuPose ? 15 : 16); ++i) if (!std::isfinite(matrix[i])) return false;
    for (int r = 0; r < 3; ++r)
    {
        double squared = 0;
        for (int c = 0; c < 3; ++c) squared += double(matrix[r*4+c])*matrix[r*4+c];
        lengths[r] = std::sqrt(squared);
        if (!std::isfinite(lengths[r]) || lengths[r] <= 0) return false;
    }
    double dots[3]; int pair = 0;
    for (int a = 0; a < 3; ++a) for (int b = a+1; b < 3; ++b, ++pair)
    {
        dots[pair] = 0;
        for (int c = 0; c < 3; ++c) dots[pair] += double(matrix[a*4+c])*matrix[b*4+c];
        const double normalized = dots[pair]/(lengths[a]*lengths[b]);
        if (!std::isfinite(dots[pair]) || !std::isfinite(normalized)) return false;
    }
    for (double length : lengths) if (std::fabs(length-1) > 0.15) return false;
    for (double dot : dots) if (std::fabs(dot) > 0.15) return false;
    return true;
}
static bool ReferenceBasisQuaternion(const float* r, float& x, float& y, float& z, float& w)
{
    const float trace = r[0]+r[4]+r[8];
    if (trace > 0)
    {
        const float s = 0.5f/sqrtf(trace+1);
        w=0.25f/s; x=(r[7]-r[5])*s; y=(r[2]-r[6])*s; z=(r[3]-r[1])*s;
    }
    else if (r[0] > r[4] && r[0] > r[8])
    {
        const float s=2*sqrtf(1+r[0]-r[4]-r[8]);
        x=0.25f*s; y=(r[1]+r[3])/s; z=(r[2]+r[6])/s; w=(r[7]-r[5])/s;
    }
    else if (r[4] > r[8])
    {
        const float s=2*sqrtf(1+r[4]-r[0]-r[8]);
        x=(r[1]+r[3])/s; y=0.25f*s; z=(r[5]+r[7])/s; w=(r[2]-r[6])/s;
    }
    else
    {
        const float s=2*sqrtf(1+r[8]-r[0]-r[4]);
        x=(r[2]+r[6])/s; y=(r[5]+r[7])/s; z=0.25f*s; w=(r[3]-r[1])/s;
    }
    const float length=sqrtf(x*x+y*y+z*z+w*w);
    if (!std::isfinite(length) || length < 1e-6f) return false;
    x/=length; y/=length; z/=length; w/=length;
    return true;
}
static void ReferenceSceneMtxInterpolate(float* out, const float* prev, const float* curr,
    double alpha, int* mode = nullptr, unsigned family = 0)
{
    if (mode) *mode=1;
    if (!memcmp(prev,curr,64)) { memcpy(out,prev,64); return; }
    memcpy(out,curr,64);
    for (int axis=0; axis<3; ++axis)
    {
        const double p=MatrixPosition(prev,axis), c=MatrixPosition(curr,axis);
        const double world=p+(c-p)*alpha;
        const double high=curr[12+axis], low=curr[3+4*axis];
        WriteSceneSplitTranslation(out,axis,high+(world-(high+low)),low);
    }
    if (family==kVehicleLocalShearFamily)
    { const bool valid=InterpolateVehicleLocalBasis(out,prev,curr,alpha); if (mode) *mode=valid?5:6; return; }
    float pr[9],cr[9]; double ps[3],cs[3]; bool pm,cm;
    if (!SceneMtxRotationBasis(prev,pr,ps,&pm) || !SceneMtxRotationBasis(curr,cr,cs,&cm))
    {
        if (InterpolateVehicleLocalBasis(out,prev,curr,alpha)) { if (mode) *mode=7; return; }
        if (mode) *mode=2; return;
    }
    if (pm!=cm) { if (mode) *mode=3; return; }
    if (mode) *mode=4;
    float px,py,pz,pw,cx,cy,cz,cw;
    if (!ReferenceBasisQuaternion(pr,px,py,pz,pw) || !ReferenceBasisQuaternion(cr,cx,cy,cz,cw)) return;
    float dot=px*cx+py*cy+pz*cz+pw*cw;
    if (dot<0) { cx=-cx;cy=-cy;cz=-cz;cw=-cw;dot=-dot; }
    dot=std::max(0.0f,std::min(1.0f,dot));
    float x,y,z,w;
    if (dot>0.9995f)
    { x=px+(cx-px)*float(alpha);y=py+(cy-py)*float(alpha);z=pz+(cz-pz)*float(alpha);w=pw+(cw-pw)*float(alpha); }
    else
    {
        const float theta=acosf(dot), sine=sinf(theta);
        const float a=sinf(float(1-alpha)*theta)/sine, b=sinf(float(alpha)*theta)/sine;
        x=a*px+b*cx;y=a*py+b*cy;z=a*pz+b*cz;w=a*pw+b*cw;
    }
    const float length=sqrtf(x*x+y*y+z*z+w*w);
    if (!std::isfinite(length) || length<1e-6f) return;
    x/=length;y/=length;z/=length;w/=length;
    const float x2=x+x,y2=y+y,z2=z+z;
    const float xx=x*x2,yy=y*y2,zz=z*z2,xy=x*y2,xz=x*z2,yz=y*z2,wx=w*x2,wy=w*y2,wz=w*z2;
    out[0]=1-(yy+zz);out[1]=xy-wz;out[2]=xz+wy;
    out[4]=xy+wz;out[5]=1-(xx+zz);out[6]=yz-wx;
    out[8]=xz-wy;out[9]=yz+wx;out[10]=1-(xx+yy);
    for (int row=0;row<3;++row)
    {
        const double scale=ps[row]+(cs[row]-ps[row])*alpha;
        for (int col=0;col<3;++col) out[row*4+col]=float(out[row*4+col]*scale);
    }
}
