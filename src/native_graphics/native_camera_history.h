#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace edf::native {
// Row-vector rigid camera-to-world transform. History never owns guest memory.
struct NativeCameraPose {
  std::array<float,16> world{};
  float fov=0;
  std::array<uint32_t,4> viewport{};
  bool operator==(const NativeCameraPose&) const = default;
};

class NativeCameraHistory {
 public:
  static bool CanInterpolate(const NativeCameraPose& a,const NativeCameraPose& b) {
    return Valid(a) && Valid(b) && !Cut(a,b);
  }
  void Reset() { initialized_=false; }
  NativeCameraPose Sample(const NativeCameraPose& pose,uint64_t tick,float fraction) {
    if(!Valid(pose) || !std::isfinite(fraction)) { Reset(); return pose; }
    if(!initialized_ || tick<tick_ || tick-tick_>1 ||
       pose.viewport!=current_.viewport ||
       (tick==tick_ && pose!=current_) || Cut(current_,pose)) {
      previous_=current_=pose; tick_=tick; initialized_=true;
    } else if(tick!=tick_) {
      previous_=current_; current_=pose; tick_=tick;
    }
    const float alpha=std::clamp(fraction,0.f,1.f);
    if(alpha==0) return previous_;
    if(alpha==1 || previous_==current_) return current_;
    auto a=Rotation(previous_.world),b=Rotation(current_.world);
    float dot=Dot(a,b);
    if(dot<0) { for(auto& v:b) v=-v; dot=-dot; }
    float left=1-alpha,right=alpha;
    if(dot<0.9995f) {
      const float angle=std::acos(std::clamp(dot,-1.f,1.f));
      const float divisor=std::sin(angle);
      left=std::sin((1-alpha)*angle)/divisor;
      right=std::sin(alpha*angle)/divisor;
    }
    for(size_t i=0;i<4;++i) a[i]=a[i]*left+b[i]*right;
    Normalize(a);
    const auto [x,y,z,w]=a;
    auto result=current_;
    result.world={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
                  2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
                  2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,
                  0,0,0,1};
    for(size_t i=12;i<15;++i)
      result.world[i]=previous_.world[i]+alpha*(current_.world[i]-previous_.world[i]);
    result.fov=previous_.fov+alpha*(current_.fov-previous_.fov);
    return result;
  }
 private:
  using Quaternion=std::array<float,4>;
  static float Dot(const Quaternion& a,const Quaternion& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
  }
  static void Normalize(Quaternion& q) {
    const auto length=std::sqrt(Dot(q,q));
    for(auto& v:q) v/=length;
  }
  static Quaternion Rotation(const std::array<float,16>& m) {
    Quaternion q{};
    const float trace=m[0]+m[5]+m[10];
    if(trace>0) {
      const float s=std::sqrt(trace+1.f)*2;
      q={(m[6]-m[9])/s,(m[8]-m[2])/s,(m[1]-m[4])/s,s/4};
    } else {
      size_t i=0;
      if(m[5]>m[0]) i=1;
      if(m[10]>m[i*5]) i=2;
      const size_t j=(i+1)%3,k=(i+2)%3;
      const float s=std::sqrt(1+m[i*5]-m[j*5]-m[k*5])*2;
      q[i]=s/4; q[j]=(m[i*4+j]+m[j*4+i])/s;
      q[k]=(m[i*4+k]+m[k*4+i])/s;
      q[3]=(m[j*4+k]-m[k*4+j])/s;
    }
    Normalize(q); return q;
  }
  static bool Valid(const NativeCameraPose& pose) {
    const auto& m=pose.world;
    for(auto v:m) if(!std::isfinite(v)) return false;
    if(!std::isfinite(pose.fov) || pose.fov<=0 || pose.fov>=3.14f) return false;
    if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>0.001f) return false;
    for(size_t i=0;i<3;++i) for(size_t j=i;j<3;++j) {
      float dot=0;
      for(size_t k=0;k<3;++k) dot+=m[i*4+k]*m[j*4+k];
      if(std::abs(dot-(i==j?1.f:0.f))>0.002f) return false;
    }
    const float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-
      m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return determinant>0.998f;
  }
  static bool Cut(const NativeCameraPose& a,const NativeCameraPose& b) {
    float distance=0;
    for(size_t i=12;i<15;++i) { const auto d=a.world[i]-b.world[i]; distance+=d*d; }
    // Discontinuous jumps must not sweep through geometry. Ordinary movement
    // and zoom blend; camera replacement also resets at the integration layer.
    return distance>50.f*50.f || std::abs(a.fov-b.fov)>0.25f ||
      std::abs(Dot(Rotation(a.world),Rotation(b.world)))<0.70710678f;
  }
  NativeCameraPose previous_{},current_{};
  uint64_t tick_=0;
  bool initialized_=false;
};
}
